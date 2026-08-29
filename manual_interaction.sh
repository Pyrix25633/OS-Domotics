#!/bin/bash
# Bash wrapper for the binary executable
# To be called after a `make build` or `make run` which creates the binary executable
exec ./bin/manual_interaction "$@"