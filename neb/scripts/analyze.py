#!/usr/bin/env python3
import sys
import math
import numpy as np
from scipy import stats

if len(sys.argv) < 2:
    print("You need to provide the file as an argument")
    exit()


def get_deliveries(proc_out):
    return list(map(lambda x: x[1:], filter(lambda x: x[0] == "d", proc_out)))

# split lines
raw = list(map(lambda x: float(x.strip()), open(sys.argv[1]).readlines()))

def ts(x):
    return x[3]

def filter_pid(raw, pid):
  return list(filter(lambda x: x[0] == pid, raw))

def analyze(lst):
    batch = lst
    descr = stats.describe(batch)
    print("Mean (us): {:.2f}".format(descr.mean))
    print("Std dev (us): {:.2f}".format(math.sqrt(descr.variance)))
    print("(Min, Max) = ({:.2f}, {:.2f})".format(descr.minmax[0], descr.minmax[1]))
    print("Median (us): {:.2f}".format(np.percentile(batch, 50)))
    print("1st %ile (us): {:.2f}".format(np.percentile(batch, 1)))
    print("60st %ile (us): {:.2f}".format(np.percentile(batch, 60)))
    print("90th %ile (us): {:.2f}".format(np.percentile(batch, 90)))
    print("99th %ile (us): {:.2f}".format(np.percentile(batch, 99)))


analyze(raw)


# for _, pid in enumerate(np.arange(1, 5)):
#     print("---")
#     print("Proc {}".format(pid))
#     analyze(list(map(ts, filter_pid(d_lines, pid))))