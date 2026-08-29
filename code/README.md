# OS-Domotics

**Team 22 - Project 2: Domotics** - Biral Mattia, Potrich Elisa, Paiola Leonardo.

## Group Work

The project has been developed by all three members of the group from scratch step by step, as can be seen by the commits of the repository on [GitHub](https://github.com/Pyrix25633/OS-Domotics) (we removed the `.git` folder because it was not requested and was quite large compared to the code base).

## Libraries

The project uses two libraries:
- `pthread`, for threads and mutexes, part of the standard system libraries;
- `ncurses`, for the Controller user interface, not used by the other executables, on Ubuntu it can be installed with `sudo apt install libncurses-dev`, it is already included in the `apt` public repository.

## How to Compile

### Targets

The `Makefile` has to be launched from the `code` folder, it supports the following targets:
- `build`, compiles the entire project;
- `clean`, removes temporary files that may be left over in case of serious errors;
- `run`, cleans, builds and then executes the Controller, which can then be used to create other devices.

### Arguments

The `--no-ncurses` argument can be passed using `ARGS="--no-ncurses"`.

### Additional Folders

The `bin` and `ipc` folders are automatically created by the `Makefile`.