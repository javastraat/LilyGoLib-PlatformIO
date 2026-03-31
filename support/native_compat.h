#ifndef T_LORA_SUPPORT_NATIVE_COMPAT_H
#define T_LORA_SUPPORT_NATIVE_COMPAT_H

#if defined(__APPLE__) && !defined(ARDUINO)

#ifdef __cplusplus
extern "C" {
#endif

static inline char *t_lora_native_itoa(int value, char *buffer, int base)
{
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    unsigned int magnitude;
    char *cursor = buffer;
    char *first_digit = buffer;

    if (base < 2 || base > 36) {
        buffer[0] = '\0';
        return buffer;
    }

    if (value < 0 && base == 10) {
        *cursor++ = '-';
        first_digit = cursor;
        magnitude = (unsigned int)(-(value + 1)) + 1U;
    } else {
        magnitude = (unsigned int)value;
    }

    do {
        *cursor++ = digits[magnitude % (unsigned int)base];
        magnitude /= (unsigned int)base;
    } while (magnitude != 0U);

    *cursor = '\0';

    for (char *left = first_digit, *right = cursor - 1; left < right; ++left, --right) {
        char tmp = *left;
        *left = *right;
        *right = tmp;
    }

    return buffer;
}

#define itoa t_lora_native_itoa

#ifdef __cplusplus
}
#endif

#endif

#endif