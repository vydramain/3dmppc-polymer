/* HIDAPI backend for the browser build over the open WebHID devices of rv_webhid.js; waiting reads and feature calls use JSPI.
   Open, close, product string, device info, reads, writes and feature reports work; the rest fail with a reason.
   WebHID gives no bus: a known pad in its Bluetooth mode or a Bluetooth Xbox pad is Bluetooth, the rest is unknown. */

#include "../hidapi/hidapi.h"

#include <limits.h>

// "webhid:" + up to 10 digits of id + NUL
#define WEBHID_PATH_BYTES 32
// Device path is this prefix plus the id; webhid_path_id reads the id back.
#define WEBHID_PATH_PREFIX "webhid:"
// rv_webhid_product_name writes at most len-1 bytes of UTF-8 and a NUL
#define WEBHID_NAME_BYTES 256
// rv_webhid.js writes the reason of its -1 to the browser console.
#define WEBHID_JS_FAILED L"the WebHID call failed; the browser console has the reason"

int rv_webhid_count(void);
int rv_webhid_id(int index);
// bluetooth is 1 for a known pad in its Bluetooth mode, else 0.
int rv_webhid_info(int id, int *vendor, int *product, int *usage_page, int *usage, int *bluetooth);
int rv_webhid_product_name(int id, char *buf, int len);
// rv_webhid_read and rv_webhid_write are synchronous: read returns a queued report or 0, write does not wait.
int rv_webhid_read(int id, unsigned char *buf, int len);
// rv_webhid_read_timeout, rv_webhid_get_feature and rv_webhid_send_feature are async: they suspend wasm through JSPI.
int rv_webhid_read_timeout(int id, unsigned char *buf, int len, int ms);
int rv_webhid_write(int id, const unsigned char *buf, int len);
int rv_webhid_get_feature(int id, unsigned char *buf, int len);
int rv_webhid_send_feature(int id, const unsigned char *buf, int len);

struct hid_device_ {
    int id;
    int nonblocking;
    // Taken at open and freed by hid_close; the JS device is owned by rv_webhid.js and stays open.
    struct hid_device_info *info;
    // Static text of the last failed call; NULL when none failed.
    const wchar_t *last_error;
};

int hid_init(void)
{
    return 0;
}

int hid_exit(void)
{
    return 0;
}

/* UTF-8 product name as wchar_t, the form SDL converts back; NULL if it cannot be read or converted. */
static wchar_t *webhid_product_string(int id)
{
    char utf8[WEBHID_NAME_BYTES];
    int len = rv_webhid_product_name(id, utf8, sizeof(utf8));
    wchar_t *name;

    if (len < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "WebHID: no product name for device %d", id);
        return NULL;
    }
    name = (wchar_t *)SDL_iconv_string("WCHAR_T", "UTF-8", utf8, (size_t)len);
    if (!name) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "WebHID: product name of device %d cannot be converted", id);
    }
    return name;
}

/* Path is "webhid:<id>"; webhid_path_id reads the id back. */
static struct hid_device_info *webhid_make_info(int id, int vendor, int product, int usage_page, int usage, int bluetooth)
{
    char path[WEBHID_PATH_BYTES];
    struct hid_device_info *info = (struct hid_device_info *)SDL_calloc(1, sizeof(*info));
    const bool bluetooth_bus = bluetooth || SDL_IsJoystickBluetoothXboxOne(vendor, product);

    if (!info) {
        return NULL;
    }
    SDL_snprintf(path, sizeof(path), "%s%d", WEBHID_PATH_PREFIX, id);
    info->path = SDL_strdup(path);
    info->vendor_id = (unsigned short)vendor;
    info->product_id = (unsigned short)product;
    info->product_string = webhid_product_string(id);
    info->usage_page = (unsigned short)usage_page;
    info->usage = (unsigned short)usage;
    // WebHID gives no serial, manufacturer, release number or USB interface; the bus is known only for Bluetooth pads:
    // a known pad of rv_webhid.js in Bluetooth mode, and a Bluetooth Xbox pad from SDL's list, since GIP takes an Xbox
    // pad of unknown bus and refuses only the Bluetooth ones.
    info->interface_number = -1;
    info->bus_type = bluetooth_bus ? HID_API_BUS_BLUETOOTH : HID_API_BUS_UNKNOWN;
    if (!info->path) {
        hid_free_enumeration(info);
        return NULL;
    }
    return info;
}

/* The id of a strict "webhid:<id>" path, or -1: ids start at 1 and have no leading zeros. */
static int webhid_path_id(const char *path)
{
    const size_t prefix_len = sizeof(WEBHID_PATH_PREFIX) - 1;
    const char *digits;
    int id = 0;

    if (!path || SDL_strncmp(path, WEBHID_PATH_PREFIX, prefix_len) != 0) {
        return -1;
    }
    digits = path + prefix_len;
    if (*digits < '1' || *digits > '9') {
        return -1;
    }
    for (; *digits; digits++) {
        int digit = *digits - '0';

        if (digit < 0 || digit > 9 || id > (INT_MAX - digit) / 10) {
            return -1;
        }
        id = id * 10 + digit;
    }
    return id;
}

/* Opens the device with this id from the rv_webhid.js table; the info is taken now and kept. */
static hid_device *webhid_open_id(int id)
{
    int vendor = 0;
    int product = 0;
    int usage_page = 0;
    int usage = 0;
    int bluetooth = 0;
    struct hid_device_info *info;
    hid_device *dev;

    if (rv_webhid_info(id, &vendor, &product, &usage_page, &usage, &bluetooth) < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "WebHID: device %d is not open, cannot open it", id);
        return NULL;
    }
    info = webhid_make_info(id, vendor, product, usage_page, usage, bluetooth);
    if (!info) {
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "WebHID: out of memory opening device %d", id);
        return NULL;
    }
    dev = (hid_device *)SDL_calloc(1, sizeof(*dev));
    if (!dev) {
        hid_free_enumeration(info);
        SDL_LogError(SDL_LOG_CATEGORY_INPUT, "WebHID: out of memory opening device %d", id);
        return NULL;
    }
    dev->id = id;
    dev->info = info;
    return dev;
}

struct hid_device_info *hid_enumerate(unsigned short vendor_id, unsigned short product_id)
{
    struct hid_device_info *head = NULL;
    struct hid_device_info **tail = &head;
    int count = rv_webhid_count();

    for (int i = 0; i < count; i++) {
        int id = rv_webhid_id(i);
        int vendor = 0;
        int product = 0;
        int usage_page = 0;
        int usage = 0;
        int bluetooth = 0;
        struct hid_device_info *info;

        if (id < 0 || rv_webhid_info(id, &vendor, &product, &usage_page, &usage, &bluetooth) < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "WebHID: cannot read device at index %d", i);
            continue;
        }
        if ((vendor_id && vendor != vendor_id) || (product_id && product != product_id)) {
            continue;
        }
        info = webhid_make_info(id, vendor, product, usage_page, usage, bluetooth);
        if (!info) {
            SDL_LogError(SDL_LOG_CATEGORY_INPUT, "WebHID: out of memory for device %d, skipped", id);
            continue;
        }
        *tail = info;
        tail = &info->next;
    }
    return head;
}

void hid_free_enumeration(struct hid_device_info *devs)
{
    while (devs) {
        struct hid_device_info *next = devs->next;

        SDL_free(devs->path);
        SDL_free(devs->serial_number);
        SDL_free(devs->manufacturer_string);
        SDL_free(devs->product_string);
        SDL_free(devs);
        devs = next;
    }
}

// WebHID has no serial numbers, so a non-empty serial matches no device.
hid_device *hid_open(unsigned short vendor_id, unsigned short product_id, const wchar_t *serial_number)
{
    struct hid_device_info *devs;
    hid_device *dev;

    if (serial_number && serial_number[0] != L'\0') {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "WebHID: devices have no serial numbers, the serial given matches none");
        return NULL;
    }
    devs = hid_enumerate(vendor_id, product_id);
    if (!devs) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT,
            "WebHID: no open device matches vendor %u product %u",
            (unsigned int)vendor_id,
            (unsigned int)product_id);
        return NULL;
    }
    dev = hid_open_path(devs->path);
    hid_free_enumeration(devs);
    return dev;
}

hid_device *hid_open_path(const char *path)
{
    int id = webhid_path_id(path);

    if (id < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "WebHID: not a WebHID path: %s", path ? path : "(null)");
        return NULL;
    }
    return webhid_open_id(id);
}

// rv_webhid.js takes int lengths; a longer buffer fails here, with the reason in last_error.
static int webhid_int_len(hid_device *dev, size_t length)
{
    if (length > (size_t)INT_MAX) {
        dev->last_error = L"the buffer is longer than INT_MAX bytes";
        return -1;
    }
    return (int)length;
}

int hid_write(hid_device *dev, const unsigned char *data, size_t length)
{
    int len = webhid_int_len(dev, length);
    int written;

    if (len < 0) {
        return -1;
    }
    written = rv_webhid_write(dev->id, data, len);
    if (written < 0) {
        dev->last_error = WEBHID_JS_FAILED;
    }
    return written;
}

// A poll (0 ms) calls the sync read, so it does not suspend wasm; other ms values wait in JS.
int hid_read_timeout(hid_device *dev, unsigned char *data, size_t length, int milliseconds)
{
    int len = webhid_int_len(dev, length);
    int n;

    if (len < 0) {
        return -1;
    }
    n = milliseconds == 0 ? rv_webhid_read(dev->id, data, len) : rv_webhid_read_timeout(dev->id, data, len, milliseconds);
    if (n < 0) {
        dev->last_error = WEBHID_JS_FAILED;
    }
    return n;
}

// hidapi: a nonblocking read polls, a blocking read waits until a report or a removal.
int hid_read(hid_device *dev, unsigned char *data, size_t length)
{
    return hid_read_timeout(dev, data, length, dev->nonblocking ? 0 : -1);
}

int hid_set_nonblocking(hid_device *dev, int nonblock)
{
    dev->nonblocking = nonblock ? 1 : 0;
    return 0;
}

// As in hidapi, data[0] is the report id.
int hid_send_feature_report(hid_device *dev, const unsigned char *data, size_t length)
{
    int len = webhid_int_len(dev, length);
    int sent;

    if (len < 0) {
        return -1;
    }
    sent = rv_webhid_send_feature(dev->id, data, len);
    if (sent < 0) {
        dev->last_error = WEBHID_JS_FAILED;
    }
    return sent;
}

// As in hidapi, data[0] is the report id on input and the report id of the reply on output.
int hid_get_feature_report(hid_device *dev, unsigned char *data, size_t length)
{
    int len = webhid_int_len(dev, length);
    int got;

    if (len < 0) {
        return -1;
    }
    got = rv_webhid_get_feature(dev->id, data, len);
    if (got < 0) {
        dev->last_error = WEBHID_JS_FAILED;
    }
    return got;
}

// WebHID has no request for an input report; the reports come through hid_read.
int hid_get_input_report(hid_device *dev, unsigned char *data, size_t length)
{
    (void)data;
    (void)length;
    dev->last_error = L"WebHID has no request for an input report";
    return -1;
}

void hid_close(hid_device *dev)
{
    hid_free_enumeration(dev->info);
    SDL_free(dev);
}

int hid_get_manufacturer_string(hid_device *dev, wchar_t *string, size_t maxlen)
{
    (void)string;
    (void)maxlen;
    dev->last_error = L"WebHID gives no manufacturer string";
    return -1;
}

// The same product name hid_enumerate gives, taken at open.
int hid_get_product_string(hid_device *dev, wchar_t *string, size_t maxlen)
{
    if (!dev->info->product_string) {
        dev->last_error = L"the product name cannot be read or converted";
        return -1;
    }
    SDL_wcslcpy(string, dev->info->product_string, maxlen);
    return 0;
}

int hid_get_serial_number_string(hid_device *dev, wchar_t *string, size_t maxlen)
{
    (void)string;
    (void)maxlen;
    dev->last_error = L"WebHID gives no serial number";
    return -1;
}

struct hid_device_info *hid_get_device_info(hid_device *dev)
{
    return dev->info;
}

int hid_get_indexed_string(hid_device *dev, int string_index, wchar_t *string, size_t maxlen)
{
    (void)string_index;
    (void)string;
    (void)maxlen;
    dev->last_error = L"WebHID gives no indexed strings";
    return -1;
}

// The only caller is the xboxone driver, under SDL_PLATFORM_LINUX, which the web build does not define.
int hid_get_report_descriptor(hid_device *dev, unsigned char *buf, size_t buf_size)
{
    (void)buf;
    (void)buf_size;
    dev->last_error = L"no report descriptor is read in the browser build";
    return -1;
}

const wchar_t *hid_error(hid_device *dev)
{
    if (!dev) {
        return L"the device could not be opened";
    }
    return dev->last_error ? dev->last_error : L"Success";
}

const struct hid_api_version *hid_version(void)
{
    static const struct hid_api_version version = {
        HID_API_VERSION_MAJOR,
        HID_API_VERSION_MINOR,
        HID_API_VERSION_PATCH,
    };

    return &version;
}

const char *hid_version_str(void)
{
    return HID_API_VERSION_STR;
}
