#!/usr/bin/env python3

import argparse
import matplotlib.pyplot as plt
import sys
import numpy as np

def filter_pid(raw, pid):
  return list(filter(lambda x: x[0] == pid, raw))

def ts(d):
  return list(map(lambda x: x[2], d))

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
    help='Flip the plot',
    )

parser.add_argument('TARGET', metavar='TARGET', help="path to data samples")

results = parser.parse_args()

flip = True

raw = list(map(lambda x: list(map(lambda y: int(y), x.strip().split())), open(results.TARGET).readlines()))

fig, ax = plt.subplots()

if results.plot_type == 'aio':
  if results.flip == True:
    ax.set(xlabel="time (ns)", ylabel="# delivered", title="deliveries over time")
    ax.plot(raw, np.arange(0, len(raw)))
  else:
    ax.set(xlabel="delivered", ylabel="time (ns)", title="time for deliveries")
    ax.plot(raw)

if results.plot_type == 'split':
  
  if results.flip == True:
    ax.set(xlabel="time (ns)", ylabel="# delivered", title="deliveries over time")
    
    for _, pid in enumerate(np.arange(1, 5)):
      t = ts(filter_pid(raw, pid))
      ax.plot(t, np.arange(0, len(t)), label="pid = {}".format(pid))
      
  else:
    ax.set(xlabel="delivered", ylabel="time (ns)", title="time for deliveries")
    
    for _, pid in enumerate(np.arange(0, 4)):
      t = ts(filter_pid(raw, pid))
      ax.plot(t, label="pid = {}".format(pid))
  ax.legend()

dest = '{}.png'.format(results.TARGET)

fig.savefig(dest)
# fig.savefig('{}.pdf'.format(target))

print('Plot saved under {}'.format(dest))



