-- license:BSD-3-Clause
-- copyright-holders:MAMEdev Team

---------------------------------------------------------------------------
--
--   regnecentralen.lua
--
--   Regnecentralen RC702/RC703/RC759 subtarget
--   Use make SUBTARGET=regnecentralen to build
--
---------------------------------------------------------------------------


--------------------------------------------------
-- Specify all the CPU cores necessary for the
-- drivers referenced in regnecentralen.lst.
--------------------------------------------------

CPUS["Z80"] = true
--CPUS["I86"] = true  -- rc759 only

--------------------------------------------------
-- Specify all the sound cores necessary for the
-- drivers referenced in regnecentralen.lst.
--------------------------------------------------

SOUNDS["BEEP"] = true
--SOUNDS["SN76496"] = true  -- rc759 only
--SOUNDS["SPEAKER"] = true  -- rc759 only

--------------------------------------------------
-- specify available video cores
--------------------------------------------------

VIDEOS["I8275"] = true
--VIDEOS["I82730"] = true  -- rc759 only

--------------------------------------------------
-- specify available machine cores
--------------------------------------------------

MACHINES["AM9517A"] = true
--MACHINES["I8255"] = true    -- rc759 only
--MACHINES["MM58167"] = true  -- rc759 only
--MACHINES["PIC8259"] = true  -- rc759 only
MACHINES["TTL7474"] = true
MACHINES["UPD765"] = true
--MACHINES["WD_FDC"] = true   -- rc759 only
MACHINES["Z80CTC"] = true
MACHINES["Z80DAISY"] = true
MACHINES["Z80PIO"] = true
MACHINES["Z80SIO"] = true

--------------------------------------------------
-- specify available bus cores
--------------------------------------------------

--BUSES["CENTRONICS"] = true  -- rc759 only
--BUSES["ISBX"] = true       -- rc759 only
BUSES["RS232"] = true

--------------------------------------------------
-- specify available formats
--------------------------------------------------

--FORMATS["RC759_DSK"] = true  -- rc759 only

--------------------------------------------------
-- This is the list of files that are necessary
-- for building all of the drivers referenced
-- in regnecentralen.lst
--------------------------------------------------

function createProjects_mame_regnecentralen(_target, _subtarget)
	project ("mame_regnecentralen")
	targetsubdir(_target .."_" .. _subtarget)
	kind (LIBTYPE)
	uuid (os.uuid("drv-mame-regnecentralen"))
	addprojectflags()
	precompiledheaders_novs()

	includedirs {
		MAME_DIR .. "src/osd",
		MAME_DIR .. "src/emu",
		MAME_DIR .. "src/devices",
		MAME_DIR .. "src/mame/shared",
		MAME_DIR .. "src/lib",
		MAME_DIR .. "src/lib/util",
		MAME_DIR .. "3rdparty",
		GEN_DIR  .. "mame/layout",
	}

files{
	MAME_DIR .. "src/mame/regnecentralen/rc702.cpp",
	--MAME_DIR .. "src/mame/regnecentralen/rc759.cpp",
	--MAME_DIR .. "src/mame/regnecentralen/rc759_kbd.cpp",
	--MAME_DIR .. "src/mame/regnecentralen/rc759_kbd.h",
}
end

function linkProjects_mame_regnecentralen(_target, _subtarget)
	links {
		"mame_regnecentralen",
	}
end
