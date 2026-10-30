/*
 * ACPI, as ath10k's mac.c uses it (the regulatory domain a laptop's firmware may name, ath10k_mac_get_wrdd_regulatory()):
 * a phone has no ACPI, and FreeBSD's header cannot be built here (it wants its platform's own). Enough for the code to
 * compile and to find that there is nothing: every lookup fails, and the driver goes on without it.
 */
#ifndef _LX_LINUX_ACPI_H_
#define _LX_LINUX_ACPI_H_

#include <linux/types.h>

typedef void *acpi_handle;
typedef u32 acpi_status;
typedef char *acpi_string;
typedef u64 acpi_size;

#define AE_OK                   0
#define AE_NOT_FOUND            5
#define ACPI_FAILURE(s)         ((s) != AE_OK)
#define ACPI_SUCCESS(s)         ((s) == AE_OK)
#define ACPI_ALLOCATE_BUFFER    ((acpi_size)-1)
#define ACPI_TYPE_INTEGER       1
#define ACPI_TYPE_PACKAGE       4
#define ACPI_HANDLE(dev)        ((acpi_handle)NULL)
#define ACPI_COMPANION(dev)     NULL
#define has_acpi_companion(dev) false

union acpi_object {
	u32 type;
	struct {
		u32 type;
		u64 value;
	} integer;
	struct {
		u32 type;
		u32 count;
		union acpi_object *elements;
	} package;
};

struct acpi_buffer {
	acpi_size length;
	void *pointer;
};

struct acpi_object_list;

static inline acpi_status
acpi_get_handle(acpi_handle parent, acpi_string path, acpi_handle *handle)
{
	return AE_NOT_FOUND;
}

static inline acpi_status
acpi_evaluate_object(acpi_handle handle, acpi_string path, struct acpi_object_list *args, struct acpi_buffer *out)
{
	return AE_NOT_FOUND;
}

#endif
