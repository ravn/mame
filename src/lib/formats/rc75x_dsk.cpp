// license: BSD-3-Clause
// copyright-holders: Dirk Best
/***************************************************************************

    Regnecentralen RC75x (RC759 Piccoline / RC750 Partner)

    Disk image format (shared by both machines).

***************************************************************************/

#include "rc75x_dsk.h"

rc75x_format::rc75x_format() : wd177x_format(formats)
{
}

const char *rc75x_format::name() const noexcept
{
	return "rc75x";
}

const char *rc75x_format::description() const noexcept
{
	return "RC750/RC759 disk image";
}

const char *rc75x_format::extensions() const noexcept
{
	return "img";
}

const rc75x_format::format rc75x_format::formats[] =
{
	{
		floppy_image::FF_525, floppy_image::DSHD, floppy_image::MFM,
		1200, 8, 77, 2, 1024, {}, 1, {}, 50, 22, 54
	},
	{}
};

const rc75x_format FLOPPY_RC75X_FORMAT;
