#!/usr/bin/env python3

import argparse
import matplotlib.pyplot as plt
import sys
import numpy as np

def filter_pid(raw, pid):
  return list(filter(lambda x: x[0] == pid, raw))

def ts(d):
  return list(map(lambda x: x[3], d))

def get_deliveries(proc_out):
    return list(map(lambda x: x[1:], filter(lambda x: x[0] == "d", proc_out)))

def str2bool(v):
    if isinstance(v, bool):
       return v
    if v.lower() in ('yes', 'true', 't', 'y', '1'):
        return True
    elif v.lower() in ('no', 'false', 'f', 'n', '0'):
        return False
    else:
        raise argparse.ArgumentTypeError('Boolean value expected.')


parser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter)

plot_choises = ['aio', 'split']
parser.add_argument(
    '-t',
    '--type',
    choices=plot_choises,
    default='aio',
    nargs='?',
    dest='plot_type',
    help='Type of the plot (default: aio - all in one)',
    )

parser.add_argument(
    '-f',
    '--flip',
    type=str2bool,
    default=True,
    nargs='?',
    dest='flip',
    help='Flip axes',
    )

parser.add_argument(
  '-p',
  '--num_proc',
  default=4,
  type=int,
  choices=[2, 3, 4],
  nargs='?',
  dest='num_proc',
  help='Number of processes'
)

parser.add_argument('TARGET', metavar='TARGET', help="path to data samples")

results = parser.parse_args()

# split lines
raw = list(map(lambda x: x.strip().split(), open(results.TARGET).readlines()))
# filter out broadcast lines
d_lines = get_deliveries(raw)
# parse line content
d_lines = [list(map(lambda y: int(y), l)) for l in d_lines]


fig, ax = plt.subplots()


if results.plot_type == 'aio':
  if results.flip == True:
    ax.set(xlabel="time (ns)", ylabel="# delivered", title="deliveries over time")
    ax.plot(ts(d_lines), np.arange(0, len(raw)))
  else:
    ax.set(xlabel="delivered", ylabel="time (ns)", title="time for deliveries")
    ax.plot(ts(d_lines))

if results.plot_type == 'split':
  
  if results.flip == True:
    ax.set(xlabel="time (ns)", ylabel="# delivered", title="deliveries over time")
    
    for _, pid in enumerate(np.arange(1, results.num_proc + 1)):
      t = ts(filter_pid(d_lines, pid))
      ax.plot(t, np.arange(0, len(t)), label="pid = {}".format(pid))
      
  else:
    ax.set(xlabel="delivered", ylabel="time (ns)", title="time for deliveries")
    
    for _, pid in enumerate(np.arange(1, results.num_proc + 1)):
      t = ts(filter_pid(d_lines, pid))
      ax.plot(t, label="pid = {}".format(pid))
  ax.legend()

dest = '{}.png'.format(results.TARGET)

fig.savefig(dest)
# fig.savefig('{}.pdf'.format(target))

print('Plot saved under {}'.format(dest))



