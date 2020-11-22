#ifndef MOBILE_DEVICE_H
#define MOBILE_DEVICE_H

#include "cftypes.h"
#include <mach/error.h>

#define kAMDSuccess ERR_SUCCESS

// opaque structures
struct am_device;
struct am_device_notification;
struct am_device_service_connection;

#define ADNCI_MSG_CONNECTED    1
#define ADNCI_MSG_DISCONNECTED 2
#define ADNCI_MSG_UNKNOWN      3

// callback info for AMDeviceNotificationSubscribe()
struct am_device_notification_callback_info
{
  am_device *dev; // device handle
  uint32_t code;  // one of ADNCI_MSG_...
};
typedef void am_device_notification_callback_t(am_device_notification_callback_info *cbi, void *arg);

// manage access to the MobileDevice library
class mobile_device_lib_t
{
  // dll handle
  void *dhandle;

  // pointers to functions in MobileDevice. not to be used directly
  mach_error_t (*_AMDeviceNotificationSubscribe)(am_device_notification_callback_t *, int, int, void *, am_device_notification **);
  mach_error_t (*_AMDeviceNotificationUnsubscribe)(am_device_notification *);
  CFStringRef (*_AMDeviceCopyDeviceIdentifier)(am_device *);
  mach_error_t (*_AMDeviceConnect)(am_device *);
  int (*_AMDeviceIsPaired)(am_device *);
  mach_error_t (*_AMDeviceValidatePairing)(am_device *);
  mach_error_t (*_AMDeviceStartSession)(am_device *);
  mach_error_t (*_AMDeviceStopSession)(am_device *);
  mach_error_t (*_AMDeviceDisconnect)(am_device *);
  mach_error_t (*_AMDeviceSecureStartService)(am_device *, CFStringRef, int *, am_device_service_connection **);
  void (*_AMDServiceConnectionInvalidate)(am_device_service_connection *);
  int (*_AMDServiceConnectionGetSocket)(am_device_service_connection *);
  ssize_t (*_AMDServiceConnectionSend)(am_device_service_connection *, const void *, size_t);
  ssize_t (*_AMDServiceConnectionReceive)(am_device_service_connection *, void *, size_t);

public:
  mobile_device_lib_t(void) { reset(); }
  ~mobile_device_lib_t(void) { unload(); }

  void reset(void) { memset(this, 0, sizeof(*this)); }
  bool load(void);
  void unload(void);

  mach_error_t AMDeviceNotificationSubscribe(am_device_notification_callback_t *, int, int, void *, am_device_notification **) const;
  mach_error_t AMDeviceNotificationUnsubscribe(am_device_notification *) const;
  CFStringRef AMDeviceCopyDeviceIdentifier(am_device *) const;
  mach_error_t AMDeviceConnect(am_device *) const;
  int AMDeviceIsPaired(am_device *) const;
  mach_error_t AMDeviceValidatePairing(am_device *) const;
  mach_error_t AMDeviceStartSession(am_device *) const;
  mach_error_t AMDeviceStopSession(am_device *) const;
  mach_error_t AMDeviceDisconnect(am_device *) const;
  mach_error_t AMDeviceSecureStartService(am_device *, CFStringRef, int *, am_device_service_connection **) const;
  void AMDServiceConnectionInvalidate(am_device_service_connection *) const;
  int AMDServiceConnectionGetSocket(am_device_service_connection *) const;
  ssize_t AMDServiceConnectionSend(am_device_service_connection *handle, const void *buf, size_t len) const;
  ssize_t AMDServiceConnectionReceive(am_device_service_connection *handle, void *buf, size_t size) const;
};

extern mobile_device_lib_t MobileDevice;

#endif // MOBILE_DEVICE_H
