/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#include "tensorflow/core/data/service/dispatcher_state.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "tensorflow/core/data/service/common.h"
#include "tensorflow/core/data/service/journal.h"
#include "tensorflow/core/data/service/journal.pb.h"
#include "tensorflow/core/platform/errors.h"
#include "tensorflow/core/platform/status.h"
#include "tensorflow/core/protobuf/data_service.pb.h"
#include "tensorflow/core/protobuf/service_config.pb.h"

namespace tensorflow {
namespace data {

DispatcherState::DispatcherState()
    : worker_index_resolver_(std::vector<std::string>{}) {}

DispatcherState::DispatcherState(
    const experimental::DispatcherConfig& dispatcher_config)
    : worker_index_resolver_(dispatcher_config.worker_addresses()) {}

Status DispatcherState::Apply(const Update& update) {
  switch (update.update_type_case()) {
    case Update::kRegisterDataset:
      RegisterDataset(update.register_dataset());
      break;
    case Update::kRegisterWorker:
      RegisterWorker(update.register_worker());
      break;
    case Update::kCreateIteration:
      CreateIteration(update.create_iteration());
      break;
    case Update::kProduceSplit:
      ProduceSplit(update.produce_split());
      break;
    case Update::kAcquireIterationClient:
      AcquireIterationClient(update.acquire_iteration_client());
      break;
    case Update::kReleaseIterationClient:
      ReleaseIterationClient(update.release_iteration_client());
      break;
    case Update::kGarbageCollectIteration:
      GarbageCollectIteration(update.garbage_collect_iteration());
      break;
    case Update::kRemoveTask:
      RemoveTask(update.remove_task());
      break;
    case Update::kCreatePendingTask:
      CreatePendingTask(update.create_pending_task());
      break;
    case Update::kClientHeartbeat:
      ClientHeartbeat(update.client_heartbeat());
      break;
    case Update::kCreateTask:
      CreateTask(update.create_task());
      break;
    case Update::kFinishTask:
      FinishTask(update.finish_task());
      break;
    case Update::UPDATE_TYPE_NOT_SET:
      return errors::Internal("Update type not set.");
  }

  return Status::OK();
}

void DispatcherState::RegisterDataset(
    const RegisterDatasetUpdate& register_dataset) {
  int64_t id = register_dataset.dataset_id();
  int64_t fingerprint = register_dataset.fingerprint();
  auto dataset =
      std::make_shared<Dataset>(id, fingerprint, register_dataset.metadata());
  DCHECK(!datasets_by_id_.contains(id));
  datasets_by_id_[id] = dataset;
  DCHECK(!datasets_by_fingerprint_.contains(fingerprint));
  datasets_by_fingerprint_[fingerprint] = dataset;
  next_available_dataset_id_ = std::max(next_available_dataset_id_, id + 1);
}

void DispatcherState::RegisterWorker(
    const RegisterWorkerUpdate& register_worker) {
  std::string address = register_worker.worker_address();
  DCHECK(!workers_.contains(address));
  workers_[address] = std::make_shared<Worker>(register_worker);
  tasks_by_worker_[address] =
      absl::flat_hash_map<int64_t, std::shared_ptr<Task>>();
  worker_index_resolver_.AddWorker(address);
}

void DispatcherState::CreateIteration(
    const CreateIterationUpdate& create_iteration) {
  int64_t iteration_id = create_iteration.iteration_id();
  IterationKey iteration_key(create_iteration.iteration_key().name(),
                             create_iteration.iteration_key().iteration());
  absl::optional<int64_t> num_consumers;
  if (create_iteration.optional_num_consumers_case() ==
      CreateIterationUpdate::kNumConsumers) {
    num_consumers = create_iteration.num_consumers();
  }
  auto iteration = std::make_shared<Iteration>(
      iteration_id, create_iteration.dataset_id(),
      create_iteration.processing_mode_def(),
      create_iteration.num_split_providers(), iteration_key, num_consumers,
      create_iteration.use_cross_trainer_cache(),
      create_iteration.target_workers());
  DCHECK(!iterations_.contains(iteration_id));
  iterations_[iteration_id] = iteration;
  tasks_by_iteration_[iteration_id] = std::vector<std::shared_ptr<Task>>();
  DCHECK(!iterations_by_key_.contains(iteration_key) ||
         iterations_by_key_[iteration_key]->garbage_collected);
  iterations_by_key_[iteration_key] = iteration;
  next_available_iteration_id_ =
      std::max(next_available_iteration_id_, iteration_id + 1);
}

void DispatcherState::ProduceSplit(const ProduceSplitUpdate& produce_split) {
  std::shared_ptr<Iteration> iteration =
      iterations_[produce_split.iteration_id()];
  DCHECK(iteration->distributed_epoch_state.has_value());
  DistributedEpochState& state = iteration->distributed_epoch_state.value();
  int64_t provider_index = produce_split.split_provider_index();
  DCHECK_EQ(produce_split.iteration(), state.iterations[provider_index]);
  if (produce_split.finished()) {
    state.iterations[provider_index]++;
    state.indices[provider_index] = 0;
    return;
  }
  state.indices[provider_index]++;
}

void DispatcherState::AcquireIterationClient(
    const AcquireIterationClientUpdate& acquire_iteration_client) {
  int64_t iteration_client_id = acquire_iteration_client.iteration_client_id();
  std::shared_ptr<Iteration>& iteration =
      iterations_for_client_ids_[iteration_client_id];
  DCHECK(!iteration);
  iteration = iterations_[acquire_iteration_client.iteration_id()];
  DCHECK(iteration);
  iteration->num_clients++;
  next_available_iteration_client_id_ =
      std::max(next_available_iteration_client_id_, iteration_client_id + 1);
}

void DispatcherState::ReleaseIterationClient(
    const ReleaseIterationClientUpdate& release_iteration_client) {
  int64_t iteration_client_id = release_iteration_client.iteration_client_id();
  std::shared_ptr<Iteration>& iteration =
      iterations_for_client_ids_[iteration_client_id];
  DCHECK(iteration);
  iteration->num_clients--;
  DCHECK_GE(iteration->num_clients, 0);
  iteration->last_client_released_micros =
      release_iteration_client.time_micros();
  iterations_for_client_ids_.erase(iteration_client_id);
}

void DispatcherState::GarbageCollectIteration(
    const GarbageCollectIterationUpdate& garbage_collect_iteration) {
  int64_t iteration_id = garbage_collect_iteration.iteration_id();
  for (auto& task : tasks_by_iteration_[iteration_id]) {
    task->finished = true;
    tasks_by_worker_[task->worker_address].erase(task->task_id);
  }
  iterations_[iteration_id]->finished = true;
  iterations_[iteration_id]->garbage_collected = true;
}

void DispatcherState::RemoveTask(const RemoveTaskUpdate& remove_task) {
  std::shared_ptr<Task>& task = tasks_[remove_task.task_id()];
  DCHECK(task);
  task->removed = true;
  auto& tasks_for_iteration =
      tasks_by_iteration_[task->iteration->iteration_id];
  for (auto it = tasks_for_iteration.begin(); it != tasks_for_iteration.end();
       ++it) {
    if ((*it)->task_id == task->task_id) {
      tasks_for_iteration.erase(it);
      break;
    }
  }
  tasks_by_worker_[task->worker_address].erase(task->task_id);
  tasks_.erase(task->task_id);
  VLOG(1) << "Removed task " << remove_task.task_id() << " from worker "
          << task->worker_address;
}

void DispatcherState::CreatePendingTask(
    const CreatePendingTaskUpdate& create_pending_task) {
  int64_t task_id = create_pending_task.task_id();
  auto& task = tasks_[task_id];
  DCHECK_EQ(task, nullptr);
  auto& iteration = iterations_[create_pending_task.iteration_id()];
  DCHECK_NE(iteration, nullptr);
  task = std::make_shared<Task>(create_pending_task, iteration);
  iteration->pending_tasks.emplace(task, create_pending_task.starting_round());
  tasks_by_worker_[create_pending_task.worker_address()][task->task_id] = task;
  next_available_task_id_ = std::max(next_available_task_id_, task_id + 1);
}

void DispatcherState::ClientHeartbeat(
    const ClientHeartbeatUpdate& client_heartbeat) {
  int64_t iteration_client_id = client_heartbeat.iteration_client_id();
  auto& iteration = iterations_for_client_ids_[iteration_client_id];
  DCHECK(!iteration->pending_tasks.empty());
  auto& task = iteration->pending_tasks.front();
  if (client_heartbeat.has_task_rejected()) {
    task.failures++;
    task.ready_consumers.clear();
    task.target_round = client_heartbeat.task_rejected().new_target_round();
  }
  if (client_heartbeat.task_accepted()) {
    task.ready_consumers.insert(iteration_client_id);
    if (task.ready_consumers.size() == iteration->num_consumers.value()) {
      VLOG(1) << "Promoting task " << task.task->task_id
              << " from pending to active";
      task.task->starting_round = task.target_round;
      tasks_by_iteration_[iteration->iteration_id].push_back(task.task);
      iteration->pending_tasks.pop();
    }
  }
}

void DispatcherState::CreateTask(const CreateTaskUpdate& create_task) {
  int64_t task_id = create_task.task_id();
  auto& task = tasks_[task_id];
  DCHECK_EQ(task, nullptr);
  auto& iteration = iterations_[create_task.iteration_id()];
  DCHECK_NE(iteration, nullptr);
  task = std::make_shared<Task>(create_task, iteration);
  tasks_by_iteration_[create_task.iteration_id()].push_back(task);
  tasks_by_worker_[create_task.worker_address()][task->task_id] = task;
  next_available_task_id_ = std::max(next_available_task_id_, task_id + 1);
}

void DispatcherState::FinishTask(const FinishTaskUpdate& finish_task) {
  VLOG(2) << "Marking task " << finish_task.task_id() << " as finished";
  int64_t task_id = finish_task.task_id();
  auto& task = tasks_[task_id];
  DCHECK(task != nullptr);
  task->finished = true;
  tasks_by_worker_[task->worker_address].erase(task->task_id);
  bool all_finished = true;
  for (const auto& task_for_iteration :
       tasks_by_iteration_[task->iteration->iteration_id]) {
    if (!task_for_iteration->finished) {
      all_finished = false;
    }
  }
  VLOG(3) << "Iteration " << task->iteration->iteration_id
          << " finished: " << all_finished;
  iterations_[task->iteration->iteration_id]->finished = all_finished;
}

int64_t DispatcherState::NextAvailableDatasetId() const {
  return next_available_dataset_id_;
}

Status DispatcherState::DatasetFromId(
    int64_t id, std::shared_ptr<const Dataset>& dataset) const {
  auto it = datasets_by_id_.find(id);
  if (it == datasets_by_id_.end()) {
    return errors::NotFound("Dataset id ", id, " not found");
  }
  dataset = it->second;
  return Status::OK();
}

Status DispatcherState::DatasetFromFingerprint(
    uint64 fingerprint, std::shared_ptr<const Dataset>& dataset) const {
  auto it = datasets_by_fingerprint_.find(fingerprint);
  if (it == datasets_by_fingerprint_.end()) {
    return errors::NotFound("Dataset fingerprint ", fingerprint, " not found");
  }
  dataset = it->second;
  return Status::OK();
}

Status DispatcherState::WorkerFromAddress(
    const std::string& address, std::shared_ptr<const Worker>& worker) const {
  auto it = workers_.find(address);
  if (it == workers_.end()) {
    return errors::NotFound("Worker with address ", address, " not found.");
  }
  worker = it->second;
  return Status::OK();
}

std::vector<std::shared_ptr<const DispatcherState::Worker>>
DispatcherState::ListWorkers() const {
  std::vector<std::shared_ptr<const Worker>> workers;
  workers.reserve(workers_.size());
  for (const auto& it : workers_) {
    workers.push_back(it.second);
  }
  return workers;
}

std::vector<std::shared_ptr<const DispatcherState::Iteration>>
DispatcherState::ListIterations() const {
  std::vector<std::shared_ptr<const DispatcherState::Iteration>> iterations;
  iterations.reserve(iterations_.size());
  for (const auto& it : iterations_) {
    iterations.push_back(it.second);
  }
  return iterations;
}

Status DispatcherState::IterationFromId(
    int64_t id, std::shared_ptr<const Iteration>& iteration) const {
  auto it = iterations_.find(id);
  if (it == iterations_.end()) {
    return errors::NotFound("Iteration id ", id, " not found");
  }
  iteration = it->second;
  return Status::OK();
}

Status DispatcherState::IterationByKey(
    IterationKey iteration_key,
    std::shared_ptr<const Iteration>& iteration) const {
  auto it = iterations_by_key_.find(iteration_key);
  if (it == iterations_by_key_.end()) {
    return errors::NotFound("Iteration key (", iteration_key.name, ", ",
                            iteration_key.iteration, ") not found");
  }
  iteration = it->second;
  return Status::OK();
}

int64_t DispatcherState::NextAvailableIterationId() const {
  return next_available_iteration_id_;
}

Status DispatcherState::IterationForIterationClientId(
    int64_t iteration_client_id, std::shared_ptr<const Iteration>& iteration) {
  iteration = iterations_for_client_ids_[iteration_client_id];
  if (!iteration) {
    return errors::NotFound("Iteration client id not found: ",
                            iteration_client_id);
  }
  return Status::OK();
}

std::vector<int64_t> DispatcherState::ListActiveClientIds() {
  std::vector<int64_t> ids;
  for (const auto& it : iterations_for_client_ids_) {
    if (it.second && !it.second->finished) {
      ids.push_back(it.first);
    }
  }
  return ids;
}

int64_t DispatcherState::NextAvailableIterationClientId() const {
  return next_available_iteration_client_id_;
}

Status DispatcherState::TaskFromId(int64_t id,
                                   std::shared_ptr<const Task>& task) const {
  auto it = tasks_.find(id);
  if (it == tasks_.end()) {
    return errors::NotFound("Task ", id, " not found");
  }
  task = it->second;
  return Status::OK();
}

Status DispatcherState::TasksForIteration(
    int64_t iteration_id,
    std::vector<std::shared_ptr<const Task>>& tasks) const {
  auto it = tasks_by_iteration_.find(iteration_id);
  if (it == tasks_by_iteration_.end()) {
    return errors::NotFound("Iteration ", iteration_id, " not found");
  }
  tasks.clear();
  tasks.reserve(it->second.size());
  for (const auto& task : it->second) {
    tasks.push_back(task);
  }
  return Status::OK();
}

Status DispatcherState::TasksForWorker(
    absl::string_view worker_address,
    std::vector<std::shared_ptr<const Task>>& tasks) const {
  tasks.clear();
  auto it = tasks_by_worker_.find(worker_address);
  if (it == tasks_by_worker_.end()) {
    return errors::NotFound("Worker ", worker_address, " not found");
  }
  const absl::flat_hash_map<int64_t, std::shared_ptr<Task>>& worker_tasks =
      it->second;
  tasks.reserve(worker_tasks.size());
  for (const auto& task : worker_tasks) {
    tasks.push_back(task.second);
  }
  return Status::OK();
}

int64_t DispatcherState::NextAvailableTaskId() const {
  return next_available_task_id_;
}

Status DispatcherState::ValidateWorker(absl::string_view worker_address) const {
  return worker_index_resolver_.ValidateWorker(worker_address);
}

StatusOr<int64_t> DispatcherState::GetWorkerIndex(
    absl::string_view worker_address) const {
  return worker_index_resolver_.GetWorkerIndex(worker_address);
}

}  // namespace data
}  // namespace tensorflow
