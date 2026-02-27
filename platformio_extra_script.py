Import("env")
import os

def after_build(source, target, env):
    print("Running merge_bin_platformio.py...")
    os.system("python merge_bin_platformio.py")

env.AddPostAction("buildprog", after_build)
