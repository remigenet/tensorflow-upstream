import tensorflow as tf

import os
import argparse

# enable xla
# tf.config.optimizer.set_jit(True)

# pick distributed strategy
strategy = tf.distribute.MirroredStrategy(
    cross_device_ops=tf.distribute.NcclAllReduce(), devices=["/gpu:0", "/gpu:1"])
print('Number of devices: {}'.format(strategy.num_replicas_in_sync))

# load data
with tf.device("/gpu:0"):
    data_1 = tf.random.uniform([4])
with tf.device("/gpu:1"):
    data_2 = tf.random.uniform([4])

print(data_1.device, data_1)
print(data_2.device, data_2)

# create model
with strategy.scope():
    # ret = tf.distribute.NcclAllReduce([data_1, data_2])
    ret = data_1+data_2

print(ret.device, ret)

parser = argparse.ArgumentParser()
parser.add_argument("--log_dir")
args = parser.parse_args()
# print(args.log_dir)

# Define the checkpoint directory to store the checkpoints.
# checkpoint_dir = os.path.join(args.log_dir, "training_checkpoints")
# logs_dir = os.path.join(args.log_dir, "logs")
# # Define the name of the checkpoint files.
# checkpoint_prefix = os.path.join(checkpoint_dir, "ckpt_{epoch}")

# train model
# STEPS_PER_EPOCH = 1000 # accuracy: 0.87
# STEPS_PER_EPOCH = 1
# model.fit(data, steps_per_epoch=STEPS_PER_EPOCH, callbacks=[
#     tf.keras.callbacks.TensorBoard(log_dir=logs_dir),
#     tf.keras.callbacks.ModelCheckpoint(filepath=checkpoint_prefix,
#                                        save_weights_only=True),
# ])
