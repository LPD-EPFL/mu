#!/usr/bin/env python3

import argparse
import numpy as np
from os import listdir
from os.path import isfile, join

parser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter)
parser.add_argument("TARGET", metavar="TARGET", help="path to output dir")
results = parser.parse_args()


def get_deliveries(proc_out):
    return list(map(lambda x: x[1:], filter(lambda x: x[0] == "d", proc_out)))


def get_broadcasts(proc_out):
    return list(map(lambda x: x[1:], filter(lambda x: x[0] == "b", proc_out)))


def parse_line(l):
    l = l.strip().split()

    if l[0] == "d":
        # [type, origin, k, m]
        return [l[0], int(l[1]), int(l[2]), int(l[3])]
    else:
        # [type, k, m]
        return [l[0], int(l[1]), int(l[2])]


def parse_file(f, procs):
    pid = int(f.split(".")[0][-1:])
    # parse earch line
    lines = [parse_line(l) for l in open(join(results.TARGET, f)).readlines()]
    bcast = get_broadcasts(lines)
    deliv = get_deliveries(lines)

    frm = dict([(p, {}) for p in procs])
    bcs = {}
    for d in deliv:
        p = d[0]
        k = d[1]
        v = d[2]

        # this is the case for not delivered expected messages for example
        # due to a process crash. We skip those entries.
        if p == 0:
            continue

        if k in frm[p]:
            print(
                "WARN: Process {} delivered more than once key {} from {}".format(
                    pid, k, p
                )
            )
        frm[p][k] = v

    for b in bcast:
        k = b[0]
        v = b[1]
        bcs[k] = v

        # same case as for deliveries above
        if p == 0:
            continue

    # split broadcasts from deliveries
    return pid, dict([("b", bcs), ("d", frm)])


def get_pid(filename):
    return int(filename.split(".")[0][-1:])


files = [
    f
    for f in listdir(results.TARGET)
    if isfile(join(results.TARGET, f)) and f.endswith(".out")
]
procs = {}
num_procs = len(files)

for f in files:
    pid, out = parse_file(f, [get_pid(f) for f in files])
    procs[pid] = out

# CHECK Agreement:
# If p and q are correct processes, p delivers (k,m) from r, and q deliveres (k,m') from r, then m=m'
for p in procs:
    print("Checking agreement for {}".format(p))
    deliv = procs[p]["d"]
    for o in deliv:
        d = deliv[o]
        for k in d:
            m = d[k]
            for q in procs:
                if p == q:
                    continue
                if k in procs[q]["d"][o] and procs[q]["d"][o][k] != m:
                    print(
                        "ERR: Process {} delivered ({},{}) but process {} delivered ({},{})".format(
                            p, k, m, q, k, procs[q]["d"][o][k]
                        )
                    )
                    exit(1)

    print("Agreement for {} satisfied".format(p))

# Check No-Creation:
# If a correct process delivers (k,m) from p, p must have broadcast (k,m)
for p in procs:
    print("Checking no-creation for {}".format(p))
    deliv = procs[p]["d"]
    for o in deliv:
        d = deliv[o]
        for k in d:
            m = d[k]
            if m != procs[o]["b"][k]:
                print(
                    "ERR: Process {} delivered ({},{}) from {}, but process {} broadcast ({},{})".format(
                        p, k, m, o, o, k, procs[o]["b"][k]
                    )
                )
                # exit(1)
    print("No-creation for {} satisfied".format(p))

# CHECK Validity:
# If a correct process p broadcasts (k,m), then all correct processes eventually deliver (k,m) from p
#
# NOTE: validity is a liveness property and may not be satisfied in the given excerpts from the runs
#       although it should be added that without bad network conditions and (really) short runs this should actually pass.
for p in procs:
    is_valid = True
    print("Checking validity for {}".format(p))
    bcast = procs[p]["b"]
    for k in bcast:
        m = bcast[k]
        for q in procs:
            if not k in procs[q]["d"][p]:
                print(
                    "WARN: Process {} broadcast ({},{}), but process {} did not deliver ({},{})".format(
                        p, k, m, q, k, m
                    )
                )
                is_valid = False
    if is_valid:
        print("Validity for {} satisfied".format(p))

print("Done.")
