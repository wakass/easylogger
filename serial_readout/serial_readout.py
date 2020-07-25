import usb.core
import usb.util


dev = usb.core.find(idVendor=0xdead, idProduct=0xbeef)
if dev is None:
    raise ValueError('Device not found')


if dev.is_kernel_driver_active(0):
    try:
        dev.detach_kernel_driver(0)
        print ("kernel driver detached")
    except usb.core.USBError as e:
        sys.exit("Could not detach kernel driver: ")
else:
    print ("no kernel driver attached")
        
# set the active configuration. With no arguments, the first
# configuration will be the active one
#dev.set_configuration()
##dev.reset()

# get an endpoint instance
cfg = dev.get_active_configuration()
intf = cfg[(0,0)]

## Get the first endpoint
ep = usb.util.find_descriptor(intf)

def hid_get_report(dev):
      """ Implements HID GetReport via USB control transfer """
      return dev.ctrl_transfer(
          0xA1, # REQUEST_TYPE_CLASS | RECIPIENT_INTERFACE | ENDPOINT_IN
          1, # GET_REPORT
          0x200, 0x00,
          64)
def hid_set_report(dev, report):
      """ Implements HID SetReport via USB control transfer """
      dev.ctrl_transfer(
          0x21, # REQUEST_TYPE_CLASS | RECIPIENT_INTERFACE | ENDPOINT_OUT
          9, # SET_REPORT
          0x200, 0x00,
          report)
      
for x in range(10):
    try:
        hid_set_report(dev,'a')
        data = hid_get_report(dev)
        
        print(data)
    except Exception as e:
        print('had except',e)
        pass
