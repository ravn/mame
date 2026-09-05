// license: BSD-3-Clause
// copyright-holders: Dirk Best
/***************************************************************************

    Regnecentralen RC75x (RC759 Piccoline / RC750 Partner)

    Disk image format (shared: both machines use the same 5.25" DS/HD MFM
    1.2 MB layout -- 77 tracks x 2 sides x 8 sectors x 1024 bytes).

***************************************************************************/

#ifndef MAME_FORMATS_RC75X_DSK_H
#define MAME_FORMATS_RC75X_DSK_H

#pragma once

#include "wd177x_dsk.h"

class rc75x_format : public wd177x_format
{
public:
	rc75x_format();

	virtual const char *name() const noexcept override;
	virtual const char *description() const noexcept override;
	virtual const char *extensions() const noexcept override;

private:
	static const format formats[];
};

extern const rc75x_format FLOPPY_RC75X_FORMAT;

#endif // MAME_FORMATS_RC75X_DSK_H
