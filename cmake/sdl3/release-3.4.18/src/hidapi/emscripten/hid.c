/* HIDAPI backend for the browser build: no devices, every open fails.
   Until WebHID replaces this file, SDL in the browser sees no HID devices through it. */

#include "../hidapi/hidapi.h"

int hid_init(void)
{
    return 0;
}

int hid_exit(void)
{
    return 0;
}

struct hid_device_info *hid_enumerate(unsigned short vendor_id, unsigned short product_id)
{
    (void)vendor_id;
    (void)product_id;
    return NULL;
}

void hid_free_enumeration(struct hid_device_info *devs)
{
    (void)devs;
}

hid_device *hid_open(unsigned short vendor_id, unsigned short product_id, const wchar_t *serial_number)
{
    (void)vendor_id;
    (void)product_id;
    (void)serial_number;
    return NULL;
}

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
    return L"no HID backend in this browser build";
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
