#include "ios_instruments_client.h"
#include "mobile_device.h"
#include <unistd.h>

static string_t device_id;              // user's preferred device (-d option)
static bool found_device = false;       // has the user's preferred device been detected?
static bool verbose = false;            // verbose mode (-v option)
static int cur_message = 0;             // current message id
static int cur_channel = 0;             // current channel id
static CFDictionaryRef channels = NULL; // list of available channels published by the instruments server
static int pid2kill = -1;               // process id to kill ("kill" option)
static const char *bid2launch = NULL;   // bundle id of app to launch ("launch" option)
static bool proclist = false;           // print the process list ("proclist" option)
static bool applist = false;            // print the application list ("applist" option)

//-----------------------------------------------------------------------------
void message_aux_t::append_int(int32_t val)
{
  append_d(buf, 10);  // empty dictionary key
  append_d(buf, 3);   // 32-bit int
  append_d(buf, val);
}

//-----------------------------------------------------------------------------
void message_aux_t::append_long(int64_t val)
{
  append_d(buf, 10);  // empty dictionary key
  append_d(buf, 4);   // 64-bit int
  append_q(buf, val);
}

//-----------------------------------------------------------------------------
void message_aux_t::append_obj(CFTypeRef obj)
{
  append_d(buf, 10);  // empty dictionary key
  append_d(buf, 2);   // archived object

  bytevec_t tmp;
  archive(&tmp, obj);

  append_d(buf, tmp.size());
  append_b(buf, tmp);
}

//-----------------------------------------------------------------------------
void message_aux_t::get_bytes(bytevec_t *out) const
{
  if ( !buf.empty() )
  {
    // the final serialized array must start with a magic qword,
    // followed by the total length of the array data as a qword,
    // followed by the array data itself.
    append_q(*out, 0x1F0);
    append_q(*out, buf.size());
    append_b(*out, buf);
  }
}

//-----------------------------------------------------------------------------
// callback that handles device notifications. called once for each connected device.
static void device_callback(am_device_notification_callback_info *cbi, void *arg)
{
  if ( cbi->code != ADNCI_MSG_CONNECTED )
    return;

  CFStringRef id = MobileDevice.AMDeviceCopyDeviceIdentifier(cbi->dev);
  string_t _device_id = to_stlstr(id);
  CFRelease(id);

  if ( !device_id.empty() && device_id != _device_id )
    return;

  found_device = true;

  if ( verbose )
    printf("found device: %s\n", _device_id.c_str());

  do
  {
    // start a session on the device
    if ( MobileDevice.AMDeviceConnect(cbi->dev) != kAMDSuccess
      || MobileDevice.AMDeviceIsPaired(cbi->dev) == 0
      || MobileDevice.AMDeviceValidatePairing(cbi->dev) != kAMDSuccess
      || MobileDevice.AMDeviceStartSession(cbi->dev) != kAMDSuccess )
    {
      fprintf(stderr, "Error: failed to start a session on the device\n");
      break;
    }

    am_device_service_connection **connptr = (am_device_service_connection **)arg;

    // launch the instruments server
    mach_error_t err = MobileDevice.AMDeviceSecureStartService(
            cbi->dev,
            CFSTR("com.apple.instruments.remoteserver"),
            NULL,
            connptr);

    if ( err != kAMDSuccess )
    {
      fprintf(stderr, "Failed to start the instruments server (0x%x). "
               "Perhaps DeveloperDiskImage.dmg is not installed on the device?\n", err);
      break;
    }

    if ( verbose )
      printf("successfully launched instruments server\n");
  }
  while ( false );

  MobileDevice.AMDeviceStopSession(cbi->dev);
  MobileDevice.AMDeviceDisconnect(cbi->dev);

  CFRunLoopStop(CFRunLoopGetCurrent());
}

//-----------------------------------------------------------------------------
// launch the instruments server on the user's device.
// returns a handle that can be used to send/receive data to/from the server.
static am_device_service_connection *start_server(void)
{
  am_device_notification *notify_handle = NULL;
  am_device_service_connection *conn = NULL;

  mach_error_t err = MobileDevice.AMDeviceNotificationSubscribe(
          device_callback,
          0,
          0,
          &conn,
          &notify_handle);

  if ( err != kAMDSuccess )
  {
    fprintf(stderr, "failed to register device notifier: 0x%x\n", err);
    return NULL;
  }

  // start a run loop, and wait for the device notifier to call our callback function.
  // if no device was detected within 3 seconds, we bail out.
  CFRunLoopRunInMode(kCFRunLoopDefaultMode, 3, false);

  MobileDevice.AMDeviceNotificationUnsubscribe(notify_handle);

  if ( conn == NULL && !found_device )
  {
    if ( device_id.empty() )
      fprintf(stderr, "Failed to find a connected device\n");
    else
      fprintf(stderr, "Failed to find device with id = %s\n", device_id.c_str());
    return NULL;
  }

  return conn;
}

//-----------------------------------------------------------------------------
// "call" an Objective-C method in the instruments server process
//   conn           server handle
//   channel        determines the object that will receive the message,
//                  obtained by a previous call to make_channel()
//   selector       method name
//   args           serialized list of arguments for the method
//   expects_reply  do we expect a return value from the method?
//                  the return value can be obtained by a subsequent call to recv_message()
static bool send_message(
        am_device_service_connection *conn,
        int channel,
        CFStringRef selector,
        const message_aux_t *args,
        bool expects_reply = true)
{
  uint32_t id = ++cur_message;

  bytevec_t aux;
  if ( args != NULL )
    args->get_bytes(&aux);

  bytevec_t sel;
  if ( selector != NULL )
    archive(&sel, selector);

  DTXMessagePayloadHeader pheader;
  // the low byte of the payload flags represents the message type.
  // so far it seems that all requests to the instruments server have message type 2.
  pheader.flags = 0x2 | (expects_reply ? 0x1000 : 0);
  pheader.auxiliaryLength = aux.size();
  pheader.totalLength = aux.size() + sel.size();

  DTXMessageHeader mheader;
  mheader.magic = 0x1F3D5B79;
  mheader.cb = sizeof(DTXMessageHeader);
  mheader.fragmentId = 0;
  mheader.fragmentCount = 1;
  mheader.length = sizeof(pheader) + pheader.totalLength;
  mheader.identifier = id;
  mheader.conversationIndex = 0;
  mheader.channelCode = channel;
  mheader.expectsReply = (expects_reply ? 1 : 0);

  bytevec_t msg;
  append_v(msg, &mheader, sizeof(mheader));
  append_v(msg, &pheader, sizeof(pheader));
  append_b(msg, aux);
  append_b(msg, sel);

  size_t msglen = msg.size();
  int sock = MobileDevice.AMDServiceConnectionGetSocket(conn);
  if ( write(sock, msg.data(), msglen) != msglen )
  {
    fprintf(stderr, "Failed to send 0x%lx bytes of message: %s\n", msglen, strerror(errno));
    return false;
  }

  return true;
}

//-----------------------------------------------------------------------------
// handle a response from the server.
//   conn    server handle
//   retobj  contains the return value for the method invoked by send_message()
//   aux     usually empty, except in specific situations (see _notifyOfPublishedCapabilities)
static bool recv_message(
        am_device_service_connection *conn,
        CFTypeRef *retobj,
        CFArrayRef *aux)
{
  uint32_t id = 0;
  bytevec_t payload;

  int sock = MobileDevice.AMDServiceConnectionGetSocket(conn);

  while ( true )
  {
    DTXMessageHeader mheader;
    ssize_t nrecv = read(sock, &mheader, sizeof(mheader));
    if ( nrecv != sizeof(mheader) )
    {
      fprintf(stderr, "failed to read message header: %s, nrecv = %lx\n", strerror(errno), nrecv);
      return false;
    }

    if ( mheader.magic != 0x1F3D5B79 )
    {
      fprintf(stderr, "bad header magic: %x\n", mheader.magic);
      return false;
    }

    if ( mheader.conversationIndex == 1 )
    {
      // the message is a response to a previous request, so it should have the same id as the request
      if ( mheader.identifier != cur_message )
      {
        fprintf(stderr, "expected response to message id=%d, got a new message with id=%d\n", cur_message, mheader.identifier);
        return false;
      }
    }
    else if ( mheader.conversationIndex == 0 )
    {
      // the message is not a response to a previous request. in this case, different iOS versions produce different results.
      // on iOS 9, the incoming message can have the same message ID has the previous message we sent to the server.
      // on later versions, the incoming message will have a new message ID. we must be aware of both situations.
      if ( mheader.identifier > cur_message )
      {
        // new message id, we must update the count on our side
        cur_message = mheader.identifier;
      }
      else if ( mheader.identifier < cur_message )
      {
        // the id must match the previous request, anything else doesn't really make sense
        fprintf(stderr, "unexpected message ID: %d\n", mheader.identifier);
        return false;
      }
    }
    else
    {
      fprintf(stderr, "invalid conversation index: %d\n", mheader.conversationIndex);
      return false;
    }

    if ( mheader.fragmentId == 0 )
    {
      id = mheader.identifier;
      // when reading multiple message fragments, the 0th fragment contains only a message header
      if ( mheader.fragmentCount > 1 )
        continue;
    }

    // read the entire payload in the current fragment
    bytevec_t frag;
    append_v(frag, &mheader, sizeof(mheader));
    frag.resize(frag.size() + mheader.length);

    uint8_t *data = frag.data() + sizeof(mheader);

    uint32_t nbytes = 0;
    while ( nbytes < mheader.length )
    {
      nrecv = read(sock, data+nbytes, mheader.length-nbytes);
      if ( nrecv <= 0 )
      {
        fprintf(stderr, "failed reading from socket: %s\n", strerror(errno));
        return false;
      }
      nbytes += nrecv;
    }

    // append to the incremental payload
    append_v(payload, data, mheader.length);

    // done reading message fragments?
    if ( mheader.fragmentId == mheader.fragmentCount - 1 )
      break;
  }

  const DTXMessagePayloadHeader *pheader = (const DTXMessagePayloadHeader *)payload.data();

  // we don't know how to decompress messages yet
  uint8_t compression = (pheader->flags & 0xFF000) >> 12;
  if ( compression != 0 )
  {
    fprintf(stderr, "message is compressed (compression type %d)\n", compression);
    return false;
  }

  // serialized object array is located just after payload header
  const uint8_t *auxptr = payload.data() + sizeof(DTXMessagePayloadHeader);
  uint32_t auxlen = pheader->auxiliaryLength;

  // archived payload object appears after the auxiliary array
  const uint8_t *objptr = auxptr + auxlen;
  uint64_t objlen = pheader->totalLength - auxlen;

  if ( auxlen != 0 && aux != NULL )
  {
    string_t errbuf;
    CFArrayRef _aux = deserialize(auxptr, auxlen, &errbuf);
    if ( _aux == NULL )
    {
      fprintf(stderr, "Error: %s\n", errbuf.c_str());
      return false;
    }
    *aux = _aux;
  }

  if ( objlen != 0 && retobj != NULL )
    *retobj = unarchive(objptr, objlen);

  return true;
}

//-----------------------------------------------------------------------------
// perform the initial client-server handshake.
// here we retrieve the list of available channels published by the instruments server.
// we can open a given channel with make_channel().
static bool perform_handshake(am_device_service_connection *conn)
{
  // I'm not sure if this argument is necessary - but Xcode uses it, so I'm using it too.
  CFMutableDictionaryRef capabilities = CFDictionaryCreateMutable(NULL, 0, NULL, NULL);

  int64_t _v1 = 1;
  int64_t _v2 = 2;

  CFNumberRef v1 = CFNumberCreate(NULL, kCFNumberSInt64Type, &_v1);
  CFNumberRef v2 = CFNumberCreate(NULL, kCFNumberSInt64Type, &_v2);

  CFDictionaryAddValue(capabilities, CFSTR("com.apple.private.DTXBlockCompression"), v2);
  CFDictionaryAddValue(capabilities, CFSTR("com.apple.private.DTXConnection"), v1);

  // serialize the dictionary
  message_aux_t args;
  args.append_obj(capabilities);

  CFRelease(capabilities);
  CFRelease(v1);
  CFRelease(v2);

  if ( !send_message(conn, 0, CFSTR("_notifyOfPublishedCapabilities:"), &args, false) )
    return false;

  CFTypeRef obj = NULL;
  CFArrayRef aux = NULL;

  // we are now expecting the server to reply with the same message.
  // a description of all available channels will be provided in the arguments list.
  if ( !recv_message(conn, &obj, &aux) || obj == NULL || aux == NULL )
  {
    fprintf(stderr, "Error: failed to receive response from _notifyOfPublishedCapabilities:\n");
    return false;
  }

  bool ok = false;
  do
  {
    if ( CFGetTypeID(obj) != CFStringGetTypeID()
      || to_stlstr((CFStringRef)obj) != "_notifyOfPublishedCapabilities:" )
    {
      fprintf(stderr, "Error: unexpected message selector: %s\n", get_description(obj).c_str());
      break;
    }

    CFDictionaryRef _channels;

    // extract the channel list from the arguments
    if ( CFArrayGetCount(aux) != 1
      || (_channels = (CFDictionaryRef)CFArrayGetValueAtIndex(aux, 0)) == NULL
      || CFGetTypeID(_channels) != CFDictionaryGetTypeID()
      || CFDictionaryGetCount(_channels) == 0 )
    {
      fprintf(stderr, "channel list has an unexpected format:\n%s\n", get_description(aux).c_str());
      break;
    }

    channels = (CFDictionaryRef)CFRetain(_channels);

    if ( verbose )
      printf("channel list:\n%s\n", get_description(channels).c_str());

    ok = true;
  }
  while ( false );

  CFRelease(obj);
  CFRelease(aux);

  return ok;
}

//-----------------------------------------------------------------------------
// establish a connection to a service in the instruments server process.
// the channel identifier should be in the list of channels returned by the server
// in perform_handshake(). after a channel is established, you can use send_message()
// to remotely invoke Objective-C methods.
static int make_channel(am_device_service_connection *conn, CFStringRef identifier)
{
  if ( !CFDictionaryContainsKey(channels, identifier) )
  {
    fprintf(stderr, "channel %s is not supported by the server\n", to_stlstr(identifier).c_str());
    return -1;
  }

  int code = ++cur_channel;

  message_aux_t args;
  args.append_int(code);
  args.append_obj(identifier);

  CFTypeRef retobj = NULL;

  // request to open the channel, expect an empty reply
  if ( !send_message(conn, 0, CFSTR("_requestChannelWithCode:identifier:"), &args)
    || !recv_message(conn, &retobj, NULL) )
  {
    return -1;
  }

  if ( retobj != NULL )
  {
    fprintf(stderr, "Error: _requestChannelWithCode:identifier: returned %s\n", get_description(retobj).c_str());
    CFRelease(retobj);
    return -1;
  }

  return code;
}

//-----------------------------------------------------------------------------
// invoke method -[DTDeviceInfoService runningProcesses]
//   args:    none
//   returns: CFArrayRef procs
static bool print_proclist(am_device_service_connection *conn)
{
  int channel = make_channel(conn, CFSTR("com.apple.instruments.server.services.deviceinfo"));
  if ( channel < 0 )
    return false;

  CFTypeRef retobj = NULL;

  if ( !send_message(conn, channel, CFSTR("runningProcesses"), NULL)
    || !recv_message(conn, &retobj, NULL)
    || retobj == NULL )
  {
    fprintf(stderr, "Error: failed to retrieve return value for runningProcesses\n");
    return false;
  }

  bool ok = true;
  if ( CFGetTypeID(retobj) == CFArrayGetTypeID() )
  {
    CFArrayRef array = (CFArrayRef)retobj;

    printf("proclist:\n");
    for ( size_t i = 0, size = CFArrayGetCount(array); i < size; i++ )
    {
      CFDictionaryRef dict = (CFDictionaryRef)CFArrayGetValueAtIndex(array, i);

      CFStringRef _name = (CFStringRef)CFDictionaryGetValue(dict, CFSTR("name"));
      string_t name = to_stlstr(_name);

      CFNumberRef _pid = (CFNumberRef)CFDictionaryGetValue(dict, CFSTR("pid"));
      int pid = 0;
      CFNumberGetValue(_pid, kCFNumberSInt32Type, &pid);

      printf("%6d %s\n", pid, name.c_str());
    }
  }
  else
  {
    fprintf(stderr, "Error: process list is not in the expected format: %s\n", get_description(retobj).c_str());
    ok = false;
  }

  CFRelease(retobj);
  return ok;
}

//-----------------------------------------------------------------------------
// invoke method -[DTApplicationListingService installedApplicationsMatching:registerUpdateToken:]
//   args:   CFDictionaryRef dict
//           CFStringRef token
//   returns CFArrayRef apps
static bool print_applist(am_device_service_connection *conn)
{
  int channel = make_channel(conn, CFSTR("com.apple.instruments.server.services.device.applictionListing"));
  if ( channel < 0 )
    return false;

  // the method expects a dictionary and a string argument.
  // pass empty values so we get descriptions for all known applications.
  CFDictionaryRef dict = CFDictionaryCreate(NULL, NULL, NULL, 0, NULL, NULL);

  message_aux_t args;
  args.append_obj(dict);
  args.append_obj(CFSTR(""));

  CFRelease(dict);

  CFTypeRef retobj = NULL;

  if ( !send_message(conn, channel, CFSTR("installedApplicationsMatching:registerUpdateToken:"), &args)
    || !recv_message(conn, &retobj, NULL)
    || retobj == NULL )
  {
    fprintf(stderr, "Error: failed to retrieve applist\n");
    return false;
  }

  bool ok = true;
  if ( CFGetTypeID(retobj) == CFArrayGetTypeID() )
  {
    CFArrayRef array = (CFArrayRef)retobj;
    for ( size_t i = 0, size = CFArrayGetCount(array); i < size; i++ )
    {
      CFDictionaryRef app_desc = (CFDictionaryRef)CFArrayGetValueAtIndex(array, i);
      printf("%s\n", get_description(app_desc).c_str());
    }
  }
  else
  {
    fprintf(stderr, "apps list has an unexpected format: %s\n", get_description(retobj).c_str());
    ok = false;
  }

  CFRelease(retobj);
  return ok;
}

//-----------------------------------------------------------------------------
// invoke method -[DTProcessControlService killPid:]
//   args:    CFNumberRef process_id
//   returns: void
static bool kill(am_device_service_connection *conn, int pid)
{
  int channel = make_channel(conn, CFSTR("com.apple.instruments.server.services.processcontrol"));
  if ( channel < 0 )
    return false;

  CFNumberRef _pid = CFNumberCreate(NULL, kCFNumberSInt32Type, &pid);

  message_aux_t args;
  args.append_obj(_pid);

  CFRelease(_pid);

  return send_message(conn, channel, CFSTR("killPid:"), &args, false);
}

//-----------------------------------------------------------------------------
// invoke method -[DTProcessControlService launchSuspendedProcessWithDevicePath:bundleIdentifier:environment:arguments:]
//   args:    CFStringRef app_path
//            CFStringRef bundle_id
//            CFArrayRef args_for_app
//            CFDictionaryRef environment_vars
//            CFDictionaryRef launch_options
//   returns: CFNumberRef pid
static bool launch(am_device_service_connection *conn, const char *_bid)
{
  int channel = make_channel(conn, CFSTR("com.apple.instruments.server.services.processcontrol"));
  if ( channel < 0 )
    return false;

  // app path: not used, just pass empty string
  CFStringRef path = CFStringCreateWithCString(NULL, "", kCFStringEncodingUTF8);
  // bundle id
  CFStringRef bid = CFStringCreateWithCString(NULL, _bid, kCFStringEncodingUTF8);
  // args for app: not used, just pass empty array
  CFArrayRef appargs = CFArrayCreate(NULL, NULL, 0, NULL);
  // environment variables: not used, just pass empty dictionary
  CFDictionaryRef env = CFDictionaryCreate(NULL, NULL, NULL, 0, NULL, NULL);

  // launch options
  int _v0 = 0; // don't suspend the process after starting it
  int _v1 = 1; // kill the application if it is already running

  CFNumberRef v0 = CFNumberCreate(NULL, kCFNumberSInt32Type, &_v0);
  CFNumberRef v1 = CFNumberCreate(NULL, kCFNumberSInt32Type, &_v1);

  const void *keys[] =
  {
    CFSTR("StartSuspendedKey"),
    CFSTR("KillExisting")
  };
  const void *values[] = { v0, v1 };
  CFDictionaryRef options = CFDictionaryCreate(
        NULL,
        keys,
        values,
        2,
        NULL,
        NULL);

  message_aux_t args;
  args.append_obj(path);
  args.append_obj(bid);
  args.append_obj(env);
  args.append_obj(appargs);
  args.append_obj(options);

  CFRelease(v1);
  CFRelease(v0);
  CFRelease(options);
  CFRelease(env);
  CFRelease(appargs);
  CFRelease(bid);
  CFRelease(path);

  CFTypeRef retobj = NULL;

  if ( !send_message(conn, channel, CFSTR("launchSuspendedProcessWithDevicePath:bundleIdentifier:environment:arguments:options:"), &args)
    || !recv_message(conn, &retobj, NULL)
    || retobj == NULL )
  {
    fprintf(stderr, "Error: failed to launch %s\n", _bid);
    return false;
  }

  bool ok = true;
  if ( CFGetTypeID(retobj) == CFNumberGetTypeID() )
  {
    CFNumberRef _pid = (CFNumberRef)retobj;
    int pid = 0;
    CFNumberGetValue(_pid, kCFNumberSInt32Type, &pid);
    printf("pid: %d\n", pid);
  }
  else
  {
    fprintf(stderr, "failed to retrieve the process ID: %s\n", get_description(retobj).c_str());
    ok = false;
  }

  CFRelease(retobj);
  return ok;
}

//-----------------------------------------------------------------------------
static void usage(const char *prog)
{
  fprintf(stderr, "usage: %s [-v] [-d <device id>] TASK <task args>\n"
           "\n"
           "This is a sample client application for the iOS Instruments server.\n"
           "It is capable of rudimentary communication with the server and can\n"
           "ask it to perform some interesting tasks.\n"
           "\n"
           "TASK can be one of the following:\n"
           "  proclist  - print a list of running processes\n"
           "  applist   - print a list of installed applications\n"
           "  launch    - launch a given app. provide the bundle id of the app to launch\n"
           "  kill      - kill a given process. provide the pid of the process to kill\n"
           "\n"
           "other args:\n"
           "  -v  more verbose output\n"
           "  -d  device ID. if empty, this app will use the first device it finds\n", prog);
}

//-----------------------------------------------------------------------------
static bool parse_args(int argc, const char **argv)
{
  if ( argc > 1 )
  {
    for ( int i = 1; i < argc; )
    {
      if ( strcmp("-v", argv[i]) == 0 )
      {
        verbose = true;
        i++;
        continue;
      }
      else if ( strcmp("-d", argv[i]) == 0 )
      {
        if ( i == argc - 1 )
        {
          fprintf(stderr, "Error: -d option requires a device id string\n");
          break;
        }
        device_id = argv[i+1];
        i += 2;
        continue;
      }

      string_t task = argv[i];

      if ( task == "proclist" )
      {
        proclist = true;
        return true;
      }
      else if ( task == "applist" )
      {
        applist = true;
        return true;
      }
      else if ( task == "kill" )
      {
        if ( i == argc - 1 )
        {
          fprintf(stderr, "Error: \"kill\" requires a process id\n");
          break;
        }
        pid2kill = atoi(argv[i+1]);
        return true;
      }
      else if ( task == "launch" )
      {
        if ( i == argc - 1 )
        {
          fprintf(stderr, "Error: \"launch\" requires a bundle id\n");
          break;
        }
        bid2launch = argv[i+1];
        return true;
      }

      fprintf(stderr, "Error, invalid task: %s\n", task.c_str());
      break;
    }
  }

  usage(argv[0]);
  return false;
}

//-----------------------------------------------------------------------------
int main(int argc, const char **argv)
{
  if ( !parse_args(argc, argv) )
    return EXIT_FAILURE;

  if ( !MobileDevice.load() )
    return EXIT_FAILURE;

  am_device_service_connection *conn = start_server();
  if ( conn == NULL )
    return EXIT_FAILURE;

  bool ok = false;
  if ( perform_handshake(conn) )
  {
    if ( proclist )
      ok = print_proclist(conn);
    else if ( applist )
      ok = print_applist(conn);
    else if ( pid2kill > 0 )
      ok = kill(conn, pid2kill);
    else if ( bid2launch != NULL )
      ok = launch(conn, bid2launch);
    else
      ok = true;

    CFRelease(channels);
  }

  MobileDevice.AMDServiceConnectionInvalidate(conn);
  CFRelease(conn);

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
