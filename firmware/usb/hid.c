#include <stdio.h>

#include "usb.h"
#include "max3421e.h"
#include "timer.h"
#include "hidparser.h"
#include "debug.h"
#include "../user_io.h"
#include "../hardware.h"

static unsigned char kbd_led_state = 0;  // default: all leds off
static unsigned char joysticks = 0;      // number of detected usb joysticks

uint8_t hid_get_joysticks(void) {
  return joysticks;
}

//get HID report descriptor 
static uint8_t hid_get_report_descr(usb_device_t *dev, uint8_t iface, uint16_t size)  {
  //  hid_debugf("%s(%x, if=%d, size=%d)", __FUNCTION__, dev->bAddress, iface, size);

  uint8_t buf[size];
  usb_hid_info_t *info = &(dev->hid_info);
  uint8_t rcode = usb_ctrl_req( dev, HID_REQ_HIDREPORT, USB_REQUEST_GET_DESCRIPTOR, 0x00, 
			      HID_DESCRIPTOR_REPORT, iface, size, buf);
 
  if(!rcode) {
    hid_debugf("HID report descriptor:");
    hexdump(buf, size, 0);

    // we got a report descriptor. Try to parse it
    if(parse_report_descriptor(buf, size)) {
      if(hid_conf[0].type == CONFIG_TYPE_JOYSTICK) {
	hid_debugf("Detected USB joystick #%d", joysticks);

	info->iface[iface].device_type = HID_DEVICE_JOYSTICK;
	info->iface[iface].conf = hid_conf[0];
	info->iface[iface].jindex = joysticks++;
      }
    }
  }
    
  return rcode;
}

static uint8_t hid_set_idle(usb_device_t *dev, uint8_t iface, uint8_t reportID, uint8_t duration ) {
  //  hid_debugf("%s(%x, if=%d id=%d, dur=%d)", __FUNCTION__, dev->bAddress, iface, reportID, duration);

  return( usb_ctrl_req( dev, HID_REQ_HIDOUT, HID_REQUEST_SET_IDLE, reportID, 
		       duration, iface, 0x0000, NULL));
}

static uint8_t hid_set_protocol(usb_device_t *dev, uint8_t iface, uint8_t protocol) {
  //  hid_debugf("%s(%x, if=%d proto=%d)", __FUNCTION__, dev->bAddress, iface, protocol);

  return( usb_ctrl_req( dev, HID_REQ_HIDOUT, HID_REQUEST_SET_PROTOCOL, protocol, 
		       0x00, iface, 0x0000, NULL));
}

static uint8_t hid_set_report(usb_device_t *dev, uint8_t iface, uint8_t report_type, uint8_t report_id, 
			      uint16_t nbytes, uint8_t* dataptr ) {
  //  hid_debugf("%s(%x, if=%d data=%x)", __FUNCTION__, dev->bAddress, iface, dataptr[0]);

  return( usb_ctrl_req(dev, HID_REQ_HIDOUT, HID_REQUEST_SET_REPORT, report_id, 
		       report_type, iface, nbytes, dataptr));
}

/* todo: handle parsing in chunks */
static uint8_t usb_hid_parse_conf(usb_device_t *dev, uint8_t conf, uint16_t len) {
  usb_hid_info_t *info = &(dev->hid_info);
  uint8_t rcode;
  bool isGoodInterface = false;

  union buf_u {
    usb_configuration_descriptor_t conf_desc;
    usb_interface_descriptor_t iface_desc;
    usb_endpoint_descriptor_t ep_desc;
    usb_hid_descriptor_t hid_desc;
    uint8_t raw[len];
  } buf, *p;

  // usb_interface_descriptor

  if(rcode = usb_get_conf_descr(dev, len, conf, &buf.conf_desc)) 
    return rcode;

  /* scan through all descriptors */
  p = &buf;
  while(len > 0) {
    switch(p->conf_desc.bDescriptorType) {
    case USB_DESCRIPTOR_CONFIGURATION:
      // hid_debugf("conf descriptor size %d", p->conf_desc.bLength);
      // we already had this, so we simply ignore it
      break;

    case USB_DESCRIPTOR_INTERFACE:
      isGoodInterface = false;
      // hid_debugf("iface descriptor size %d", p->iface_desc.bLength);

      /* check the interface descriptors for supported class */

      // only HID interfaces are supported
      if(p->iface_desc.bInterfaceClass == USB_CLASS_HID) {
	puts("iface is HID");

	if(info->bNumIfaces < MAX_IFACES) {
	  // ok, let's use this interface
	  isGoodInterface = true;

	  info->iface[info->bNumIfaces].iface_idx = p->iface_desc.bInterfaceNumber;
	  info->iface[info->bNumIfaces].has_boot_mode = false;
	  info->iface[info->bNumIfaces].device_type = HID_DEVICE_UNKNOWN;
	  info->iface[info->bNumIfaces].conf.type = CONFIG_TYPE_NONE;

	  if(p->iface_desc.bInterfaceSubClass == HID_BOOT_INTF_SUBCLASS) {
	    // hid_debugf("Iface %d is Boot sub class", info->bNumIfaces);
	    info->iface[info->bNumIfaces].has_boot_mode = true;
	  }
	  
	  switch(p->iface_desc.bInterfaceProtocol) {
	  case HID_PROTOCOL_NONE:
	    hid_debugf("HID protocol is NONE");
	    break;
	    
	  case HID_PROTOCOL_KEYBOARD:
	    hid_debugf("HID protocol is KEYBOARD");
	    info->iface[info->bNumIfaces].device_type = HID_DEVICE_KEYBOARD;
	    break;
	    
	  case HID_PROTOCOL_MOUSE:
	    hid_debugf("HID protocol is MOUSE");
	    info->iface[info->bNumIfaces].device_type = HID_DEVICE_MOUSE;
	    break;
	    
	  default:
	    hid_debugf("HID protocol is %d", p->iface_desc.bInterfaceProtocol);
	    break;
	  }
	}
      }
      break;

    case USB_DESCRIPTOR_ENDPOINT:
      //      hid_debugf("endpoint descriptor size %d", p->ep_desc.bLength);

      if(isGoodInterface) {

	// only interrupt in endpoints are supported
	if ((p->ep_desc.bmAttributes & 0x03) == 3 && (p->ep_desc.bEndpointAddress & 0x80) == 0x80) {
	  hid_debugf("endpoint %d, interval = %dms", 
		  p->ep_desc.bEndpointAddress & 0x0F, p->ep_desc.bInterval);

	  // Fill in the endpoint info structure
	  uint8_t epidx = info->bNumIfaces;
	  info->iface[epidx].interval = p->ep_desc.bInterval;
	  info->iface[epidx].ep.epAddr	 = (p->ep_desc.bEndpointAddress & 0x0F);
	  info->iface[epidx].ep.maxPktSize = p->ep_desc.wMaxPacketSize[0];
	  info->iface[epidx].ep.epAttribs	 = 0;
	  info->iface[epidx].ep.bmNakPower = USB_NAK_NOWAIT;
	  info->bNumIfaces++;
	}
      }
      break;

    case HID_DESCRIPTOR_HID:
      hid_debugf("hid descriptor size %d", p->ep_desc.bLength);

      if(isGoodInterface) {
	// we need a report descriptor
	if(p->hid_desc.bDescrType == HID_DESCRIPTOR_REPORT) {
	  uint16_t len = p->hid_desc.wDescriptorLength[0] + 
	    256 * p->hid_desc.wDescriptorLength[1];
	  hid_debugf(" -> report descriptor size = %d", len);
	  
	  info->iface[info->bNumIfaces].report_desc_size = len;
	}
      }
      break;

    default:
      hid_debugf("unsupported descriptor type %d size %d", p->raw[1], p->raw[0]);
    }

    // advance to next descriptor
    len -= p->conf_desc.bLength;
    p = (union buf_u*)(p->raw + p->conf_desc.bLength);
  }
  
  if(len != 0) {
    hid_debugf("Config underrun: %d", len);
    return USB_ERROR_CONFIGURAION_SIZE_MISMATCH;
  }

  return 0;
}

static uint8_t usb_hid_init(usb_device_t *dev) {
  hid_debugf("%s(%x)", __FUNCTION__, dev->bAddress);

  uint8_t rcode;
  uint8_t i;

  usb_hid_info_t *info = &(dev->hid_info);
  
  union {
    usb_device_descriptor_t dev_desc;
    usb_configuration_descriptor_t conf_desc;
  } buf;

  // reset status
  info->bPollEnable = false;
  info->bNumIfaces = 0;

  for(i=0;i<MAX_IFACES;i++) {
    info->iface[i].qNextPollTime = 0;
    info->iface[i].ep.epAddr	 = i;
    info->iface[i].ep.maxPktSize = 8;
    info->iface[i].ep.epAttribs	 = 0;
    info->iface[i].ep.bmNakPower = USB_NAK_MAX_POWER;
  }

  // try to re-read full device descriptor from newly assigned address
  if(rcode = usb_get_dev_descr( dev, sizeof(usb_device_descriptor_t), &buf.dev_desc ))
    return rcode;

  uint8_t num_of_conf = buf.dev_desc.bNumConfigurations;
  //  hid_debugf("number of configurations: %d", num_of_conf);

  for(i=0; i<num_of_conf; i++) {
    if(rcode = usb_get_conf_descr(dev, sizeof(usb_configuration_descriptor_t), i, &buf.conf_desc)) 
      return rcode;
    
    //    hid_debugf("conf descriptor %d has total size %d", i, buf.conf_desc.wTotalLength);

    // parse directly if it already fitted completely into the buffer
    usb_hid_parse_conf(dev, i, buf.conf_desc.wTotalLength);
  }

  // check if we found valid hid interfaces
  if(!info->bNumIfaces) {
    hid_debugf("no hid interfaces found");
    return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;
  }

  // Set Configuration Value
  rcode = usb_set_conf(dev, buf.conf_desc.bConfigurationValue);

  // process all supported interfaces
  for(i=0; i<info->bNumIfaces; i++) {
    // no boot mode, try to parse HID report descriptor
    if(!info->iface[i].has_boot_mode) {
      rcode = hid_get_report_descr(dev, 
	   info->iface[i].iface_idx, info->iface[i].report_desc_size);
      if (rcode)
	return rcode;
    }

    rcode = hid_set_idle(dev, info->iface[i].iface_idx, 0, 0);
    if (rcode && rcode != hrSTALL)
      return rcode;

    // enable boot mode
    if(info->iface[i].has_boot_mode)
      hid_set_protocol(dev, info->iface[i].iface_idx, HID_BOOT_PROTOCOL);
  }
  
  puts("HID configured");

  // update leds
  for(i=0;i<MAX_IFACES;i++)
    if(dev->hid_info.iface[i].device_type == HID_DEVICE_KEYBOARD)
      hid_set_report(dev, dev->hid_info.iface[i].iface_idx, 2, 0, 1, &kbd_led_state);

  info->bPollEnable = true;
  return 0;
}

static uint8_t usb_hid_release(usb_device_t *dev) {
  usb_hid_info_t *info = &(dev->hid_info);

  puts(__FUNCTION__);

  uint8_t i;
  // check if a joystick is released
  for(i=0;i<info->bNumIfaces;i++) {
    if(info->iface[i].device_type == HID_DEVICE_JOYSTICK) {
      uint8_t c_jindex = info->iface[i].jindex;
      hid_debugf("releasing joystick #%d, renumbering", c_jindex);

      // walk through all devices and search for sticks with a higher id

      // search for all joystick interfaces on all hid devices
      usb_device_t *dev = usb_get_devices();
      uint8_t j;
      for(j=0;j<USB_NUMDEVICES;j++) {
	if(dev[j].bAddress && (dev[j].class == &usb_hid_class)) {
	  // search for joystick interfaces
	  uint8_t k;
	  for(k=0;k<MAX_IFACES;k++) {
	    if(dev[j].hid_info.iface[k].device_type == HID_DEVICE_JOYSTICK) {
	      if(dev[j].hid_info.iface[k].jindex > c_jindex) {
		hid_debugf("decreasing jindex of dev #%d from %d to %d", j, 
			dev[j].hid_info.iface[k].jindex, dev[j].hid_info.iface[k].jindex-1);
		dev[j].hid_info.iface[k].jindex--;
	      }
	    }
	  }
	}
      }
      // one less joystick in the system ...
      joysticks--;
    }
  }

  return 0;
}

static uint8_t usb_hid_poll(usb_device_t *dev) {
  usb_hid_info_t *info = &(dev->hid_info);
  int8_t i;

  if (!info->bPollEnable)
    return 0;
  
  for(i=0;i<info->bNumIfaces;i++) {
    usb_hid_iface_info_t *iface = info->iface+i;
    
    if (iface->qNextPollTime <= timer_get_msec()) {
      //      hid_debugf("poll %d...", iface->ep.epAddr);
      
      uint16_t read = iface->ep.maxPktSize;
      uint8_t buf[iface->ep.maxPktSize];
      uint8_t rcode = 
	usb_in_transfer(dev, &(iface->ep), &read, buf);

      if (rcode) {
	if (rcode != hrNAK)
	  hid_debugf("%s() error: %d", __FUNCTION__, rcode);
      } else {

	// successfully received some bytes
	if(iface->has_boot_mode) {
	  if(iface->device_type == HID_DEVICE_MOUSE) {
	    // boot mouse needs at least three bytes
	    if(read >= 3) {
	      // forward all three bytes to the user_io layer
	      user_io_mouse(buf[0], buf[1], buf[2]);
	    }
	  }

	  if(iface->device_type == HID_DEVICE_KEYBOARD) {
	    // boot kbd needs at least eight bytes
	    if(read >= 8) {
	      user_io_kbd(buf[0], buf+2);
	    }
	  }
	}

	if(iface->device_type == HID_DEVICE_JOYSTICK) {
	  hid_config_t *conf = &iface->conf;
	  if(read >= conf->report_size) {
	    uint8_t jmap = 0;
	    uint8_t ax,ay,idx;

	    //	    hid_debugf("Joystick data:");
	    //	    hexdump(buf, read, 0);

	    // currently only byte sized axes are allowed
	    ax = buf[conf->joystick.axis_byte_offset[0]];
	    if(ax <  64) jmap |= JOY_LEFT;
	    if(ax > 192) jmap |= JOY_RIGHT;
	    ay = buf[conf->joystick.axis_byte_offset[1]];
	    if(ay <  64) jmap |= JOY_UP;
	    if(ay > 192) jmap |= JOY_DOWN;

	    //	    iprintf("ax = %d ay = %d\n", ax, ay);
	    
	    // ... and buttons
            if(buf[conf->joystick.button_byte_offset] & 
               conf->joystick.button_bitmask[0]) jmap |= JOY_BTN1;
            if(buf[conf->joystick.button_byte_offset] & 
               conf->joystick.button_bitmask[1]) jmap |= JOY_BTN2;

	    // swap joystick 0 and 1 since 1 is the one 
	    // used primarily on most systems
	    idx = iface->jindex;
	    if(idx == 0)      idx = 1;
	    else if(idx == 1) idx = 0;
	    
	    // check if joystick state has changed
	    if(jmap != iface->jmap) {
	      //	      iprintf("jmap %d changed to %x\n", ax, jmap);

	      // and feed into joystick input system
	      user_io_digital_joystick(idx, jmap);
	      iface->jmap = jmap;
	    }

	    // also send analog values
	    user_io_analog_joystick(idx, ax-128, ay-128);
	  }
	}
      }
      iface->qNextPollTime += iface->interval;   // poll at requested rate
    }
  }

  return 0;
}

void hid_set_kbd_led(unsigned char led, bool on) {
  // check if led state has changed
  if( (on && !(kbd_led_state&led)) || (!on && (kbd_led_state&led))) {
    if(on) kbd_led_state |=  led;
    else   kbd_led_state &= ~led;

    // search for all keyboard interfaces on all hid devices
    usb_device_t *dev = usb_get_devices();
    int i;
    for(i=0;i<USB_NUMDEVICES;i++) {
      if(dev[i].bAddress && (dev[i].class == &usb_hid_class)) {
	// search for keyboard interfaces
	int j;
	for(j=0;j<MAX_IFACES;j++)
	  if(dev[i].hid_info.iface[j].device_type == HID_DEVICE_KEYBOARD)
	    hid_set_report(dev+i, dev[i].hid_info.iface[j].iface_idx, 2, 0, 1, &kbd_led_state);
      }
    }
  }
}

const usb_device_class_config_t usb_hid_class = {
  usb_hid_init, usb_hid_release, usb_hid_poll };  

