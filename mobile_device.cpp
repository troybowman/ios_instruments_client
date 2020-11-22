#include "mobile_device.h"
#include <err.h>
#include <dlfcn.h>

mobile_device_lib_t MobileDevice;

//------------------------------------------------------------------------------
bool mobile_device_lib_t::load(void)
{
  if ( dhandle != NULL )
    return true;

  const char *md_path = "/System/Library/PrivateFrameworks/MobileDevice.framework/MobileDevice";
  dhandle = dlopen(md_path, RTLD_NOW);
  if ( dhandle == NULL )
  {
    fprintf(stderr, "dlopen() failed for %s: %s", md_path, dlerror());
    return false;
  }

#define BINDFUN(name, type)                                              \
  _##name = reinterpret_cast<type>(dlsym(dhandle, #name));               \
  if ( _##name == NULL )                                                 \
  {                                                                      \
    unload();                                                            \
    fprintf(stderr, "Could not find function " #name " in %s", md_path); \
    return false;                                                        \
  }

  BINDFUN(AMDeviceNotificationSubscribe, mach_error_t (*)(am_device_notification_callback_t *, int, int, void *, am_device_notification **));
  BINDFUN(AMDeviceNotificationUnsubscribe, mach_error_t (*)(am_device_notification *));
  BINDFUN(AMDeviceCopyDeviceIdentifier, CFStringRef (*)(am_device *));
  BINDFUN(AMDeviceConnect, mach_error_t (*)(am_device *));
  BINDFUN(AMDeviceIsPaired, int (*)(am_device *));
  BINDFUN(AMDeviceValidatePairing, mach_error_t (*)(am_device *));
  BINDFUN(AMDeviceStartSession, mach_error_t (*)(am_device *));
  BINDFUN(AMDeviceStopSession, mach_error_t (*)(am_device *));
  BINDFUN(AMDeviceDisconnect, mach_error_t (*)(am_device *));
  BINDFUN(AMDeviceSecureStartService, mach_error_t (*)(am_device *, CFStringRef, int *, am_device_service_connection **));
  BINDFUN(AMDServiceConnectionInvalidate, void (*)(am_device_service_connection *));
  BINDFUN(AMDServiceConnectionGetSocket, int (*)(am_device_service_connection *));
  BINDFUN(AMDServiceConnectionSend, ssize_t (*)(am_device_service_connection *, const void *, size_t));
  BINDFUN(AMDServiceConnectionReceive, ssize_t (*)(am_device_service_connection *, void *, size_t));

#undef BINDFUN

  return true;
}

//------------------------------------------------------------------------------
void mobile_device_lib_t::unload()
{
  if ( dhandle != NULL )
  {
    dlclose(dhandle);
    reset();
  }
}

//------------------------------------------------------------------------------
mach_error_t mobile_device_lib_t::AMDeviceNotificationSubscribe(
        am_device_notification_callback_t *callback,
        int unused1,
        int unused2,
        void *arg,
        am_device_notification **hptr) const
{
  return _AMDeviceNotificationSubscribe(callback, unused1, unused2, arg, hptr);
}

//------------------------------------------------------------------------------
mach_error_t mobile_device_lib_t::AMDeviceNotificationUnsubscribe(am_device_notification *handle) const
{
  return _AMDeviceNotificationUnsubscribe(handle);
}

//------------------------------------------------------------------------------
CFStringRef mobile_device_lib_t::AMDeviceCopyDeviceIdentifier(am_device *device) const
{
  return _AMDeviceCopyDeviceIdentifier(device);
}

//------------------------------------------------------------------------------
mach_error_t mobile_device_lib_t::AMDeviceConnect(am_device *device) const
{
  return _AMDeviceConnect(device);
}

//------------------------------------------------------------------------------
int mobile_device_lib_t::AMDeviceIsPaired(am_device *device) const
{
  return _AMDeviceIsPaired(device);
}

//------------------------------------------------------------------------------
mach_error_t mobile_device_lib_t::AMDeviceValidatePairing(am_device *device) const
{
  return _AMDeviceValidatePairing(device);
}

//------------------------------------------------------------------------------
mach_error_t mobile_device_lib_t::AMDeviceStartSession(am_device *device) const
{
  return _AMDeviceStartSession(device);
}

//------------------------------------------------------------------------------
mach_error_t mobile_device_lib_t::AMDeviceStopSession(am_device *device) const
{
  return _AMDeviceStopSession(device);
}

//------------------------------------------------------------------------------
mach_error_t mobile_device_lib_t::AMDeviceDisconnect(am_device *device) const
{
  return _AMDeviceDisconnect(device);
}

//------------------------------------------------------------------------------
mach_error_t mobile_device_lib_t::AMDeviceSecureStartService(
        am_device *device,
        CFStringRef name,
        int *unused,
        am_device_service_connection **hptr) const
{
  return _AMDeviceSecureStartService(device, name, unused, hptr);
}

//------------------------------------------------------------------------------
void mobile_device_lib_t::AMDServiceConnectionInvalidate(am_device_service_connection *handle) const
{
  _AMDServiceConnectionInvalidate(handle);
}

//------------------------------------------------------------------------------
int mobile_device_lib_t::AMDServiceConnectionGetSocket(am_device_service_connection *handle) const
{
  return _AMDServiceConnectionGetSocket(handle);
}

//------------------------------------------------------------------------------
ssize_t mobile_device_lib_t::AMDServiceConnectionSend(
        am_device_service_connection *handle,
        const void *buf,
        size_t len) const
{
  return _AMDServiceConnectionSend(handle, buf, len);
}

//------------------------------------------------------------------------------
ssize_t mobile_device_lib_t::AMDServiceConnectionReceive(
        am_device_service_connection *handle,
        void *buf,
        size_t size) const
{
  return _AMDServiceConnectionReceive(handle, buf, size);
}
