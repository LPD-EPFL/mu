# NEB Scripts

## Benchmarking

Scripts for benchmarking are meant to be run from the local machine.

## Plotting

The `plot.py` script creates and stores a plot as png when provided with _one_
process output log. Example invocation:

`./plot.py -t split data/2020-04-22/18:56/lpdquatro2.out -p 3`

type `./plot.py --help` for details about possible args.

## Validation

The `validate.py` analyses the files gathered and checks if all the properties of NEB are satisfied.

Invoke the script by specifying the folder with the outputs as first argument, e.g.:

```sh
./validate.py data/2020-04-22/18:56
```

Note: if the script analyzes outputs gathered during a run where some processes
were byzantine - one can at most (and should!) expect that the __agreement__ property
is satisfied.
