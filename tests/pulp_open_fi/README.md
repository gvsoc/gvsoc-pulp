# Overview

The CI test invokes the Makefile which then executes `fcm.py`. This Python script uses `ficlib` to inject bitflips in 3 runs. It is designed to result in 1 OK, 1 SDC, and 1 DUE outcomes.
