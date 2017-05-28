from com.arm.debug.flashprogrammer.IFlashClient import MessageLevel

from flashprogrammer.device import ensureDeviceOpen
from flashprogrammer.execution import ensureDeviceStopped, writeRegs
from flashprogrammer.device_memory import writeToTarget, readFromTarget, intToBytes, intFromBytes

import time


def setup(client, services):
    # get a connection to the core
    conn = services.getConnection()
    dev = conn.getDeviceInterfaces().get("Cortex-A9")
    ensureDeviceOpen(dev)
    ensureDeviceStopped(dev)
    writeRegs (dev, [("R13", 0x20030000)])
