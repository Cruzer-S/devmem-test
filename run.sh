#!/bin/bash
# <is_server> <address> <port> <is_dma> <nqueue> <interface>

ADDRESS=192.168.0.100
PORT=1585
NQUEUE=1
INTERFACE=enp12s0f2np2

SERVER=1
DMA=0

make run ARGUMENTS="$SERVER $ADDRESS $PORT $DMA $NQUEUE $INTERFACE"
