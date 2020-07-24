import usb.core
import usb.util


dev = usb.core.find(idVendor=0x1020, idProduct=0xe131)
if dev is None:
    raise ValueError('Device not found')

dev.set_configuration()

# get an endpoint instance
cfg = dev.get_active_configuration()
intf = cfg[(0,0)]

## Get the first endpoint
ep = usb.util.find_descriptor(intf)

data =ep.read(1)
print(data,1)
