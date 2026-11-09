/* HIDAPI backend for the browser build: the devices are the open WebHID devices of rv_webhid.js.
   Opening is not implemented yet, so every open fails. */

#include "../hidapi/hidapi.h"

// "webhid:" + up to 10 digits of id + NUL
#define WEBHID_PATH_BYTES 32
// rv_webhid_product_name writes at most len-1 bytes of UTF-8 and a NUL
#define WEBHID_NAME_BYTES 256

int rv_webhid_count(void);
int rv_webhid_id(int index);
int rv_webhid_info(int id, int *vendor, int *product, int *usage_page, int *usage);
int rv_webhid_product_name(int id, char *buf, int len);

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

/* Path is "webhid:<id>", the id from rv_webhid_id; opening by that id is not implemented yet. */
static struct hid_device_info *webhid_make_info(int id, int vendor, int product, int usage_page, int usage)
{
    char path[WEBHID_PATH_BYTES];
    struct hid_device_info *info = (struct hid_device_info *)SDL_calloc(1, sizeof(*info));

    if (!info) {
        return NULL;
    }
    SDL_snprintf(path, sizeof(path), "webhid:%d", id);
    info->path = SDL_strdup(path);
    info->vendor_id = (unsigned short)vendor;
    info->product_id = (unsigned short)product;
    info->product_string = webhid_product_string(id);
    info->usage_page = (unsigned short)usage_page;
    info->usage = (unsigned short)usage;
    // WebHID gives no serial, manufacturer, release number, USB interface or bus.
    info->interface_number = -1;
    info->bus_type = HID_API_BUS_UNKNOWN;
    if (!info->path) {
        hid_free_enumeration(info);
        return NULL;
    }
    return info;
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
        struct hid_device_info *info;

        if (id < 0 || rv_webhid_info(id, &vendor, &product, &usage_page, &usage) < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "WebHID: cannot read device at index %d", i);
            continue;
        }
        if ((vendor_id && vendor != vendor_id) || (product_id && product != product_id)) {
            continue;
        }
        info = webhid_make_info(id, vendor, product, usage_page, usage);
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

// Opening is not implemented yet.
hid_device *hid_open(unsigned short vendor_id, unsigned short product_id, const wchar_t *serial_number)
{
    (void)vendor_id;
    (void)product_id;
    (void)serial_number;
    return NULL;
}

// Opening is not implemented yet.
hid_device *hid_open_path(const char *path)
{
    (void)path;
    return NULL;
}

int hid_write(hid_device *dev, const unsigned char *data, size_t length)
{
    (void)dev;
    (void)data;
    (void)length;
    return -1;
}

int hid_read_timeout(hid_device *dev, unsigned char *data, size_t length, int milliseconds)
{
    (void)dev;
    (void)data;
    (void)length;
    (void)milliseconds;
    return -1;
}

int hid_read(hid_device *dev, unsigned char *data, size_t length)
{
    (void)dev;
    (void)data;
    (void)length;
    return -1;
}

int hid_set_nonblocking(hid_device *dev, int nonblock)
{
    (void)dev;
    (void)nonblock;
    return -1;
}

int hid_send_feature_report(hid_device *dev, const unsigned char *data, size_t length)
{
    (void)dev;
    (void)data;
    (void)length;
    return -1;
}

int hid_get_feature_report(hid_device *dev, unsigned char *data, size_t length)
{
    (void)dev;
    (void)data;
    (void)length;
    return -1;
}

int hid_get_input_report(hid_device *dev, unsigned char *data, size_t length)
{
    (void)dev;
    (void)data;
    (void)length;
    return -1;
}

void hid_close(hid_device *dev)
{
    (void)dev;
}

int hid_get_manufacturer_string(hid_device *dev, wchar_t *string, size_t maxlen)
{
    (void)dev;
    (void)string;
    (void)maxlen;
    return -1;
}

int hid_get_product_string(hid_device *dev, wchar_t *string, size_t maxlen)
{
    (void)dev;
    (void)string;
    (void)maxlen;
    return -1;
}

int hid_get_serial_number_string(hid_device *dev, wchar_t *string, size_t maxlen)
{
    (void)dev;
    (void)string;
    (void)maxlen;
    return -1;
}

struct hid_device_info *hid_get_device_info(hid_device *dev)
{
    (void)dev;
    return NULL;
}

int hid_get_indexed_string(hid_device *dev, int string_index, wchar_t *string, size_t maxlen)
{
    (void)dev;
    (void)string_index;
    (void)string;
    (void)maxlen;
    return -1;
}

int hid_get_report_descriptor(hid_device *dev, unsigned char *buf, size_t buf_size)
{
    (void)dev;
    (void)buf;
    (void)buf_size;
    return -1;
}

const wchar_t *hid_error(hid_device *dev)
{
    (void)dev;
    return L"opening HID devices is not implemented in the browser build yet";
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
