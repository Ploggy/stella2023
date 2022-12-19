//============================================================================
//
//   SSSS    tt          lll  lll
//  SS  SS   tt           ll   ll
//  SS     tttttt  eeee   ll   ll   aaaa
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2022 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//============================================================================

#ifndef CARTRIDGEBR_HXX
#define CARTRIDGEBR_HXX

#include "bspf.hxx"
#include "CartEnhanced.hxx"
#include "System.hxx"
#ifdef DEBUGGER_SUPPORT
#include "Cart0FA0Widget.hxx"
#endif

/**
  Cartridge class used for some brazilian 8K bankswitched games. There
  are two 4K banks, which are switched by accessing
  (address & $16A0) = $06a0 (bank 0) and = $06c0 (bank 1).
  Actual addresses used by these carts are e.g. $0FA0, $0FC0 and $EFC0.
  The code accepts further potential hotspot addresses.

  @author  Thomas Jentzsch
*/
class Cartridge0FA0 : public CartridgeEnhanced
{
  friend class Cartridge0FA0Widget;

  public:
    /**
      Create a new cartridge using the specified image

      @param image         Pointer to the ROM image
      @param size          The size of the ROM image
      @param md5           The md5sum of the ROM image
      @param settings      A reference to the various settings (read-only)
    */
    Cartridge0FA0(const ByteBuffer& image, size_t size, string_view md5,
      const Settings& settings);
    ~Cartridge0FA0() override = default;

  public:
    /**
      Install cartridge in the specified system.  Invoked by the system
      when the cartridge is attached to it.

      @param system The system the device should install itself in
    */
    void install(System& system) override;

    /**
      Get a descriptor for the device name (used in error checking).

      @return The name of the object
    */
    string name() const override {
      return "Cartridge0FA0";
    }

  #ifdef DEBUGGER_SUPPORT
    /**
      Get debugger widget responsible for accessing the inner workings
      of the cart.
    */
    CartDebugWidget* debugWidget(GuiObject* boss, const GUI::Font& lfont,
      const GUI::Font& nfont, int x, int y, int w, int h) override
    {
      return new Cartridge0FA0Widget(boss, lfont, nfont, x, y, w, h, *this);
    }
  #endif

  public:
    /**
      Get the byte at the specified address.

      @return The byte at the specified address
    */
    uInt8 peek(uInt16 address) override;

    /**
      Change the byte at the specified address to the given value

      @param address The address where the value should be stored
      @param value The value to be stored at the address
      @return  True if the poke changed the device address space, else false
    */
    bool poke(uInt16 address, uInt8 value) override;

  private:
    /**
      Checks if startup bank randomization is enabled.  For this scheme,
      randomization is not supported (see above).
    */
    bool randomStartBank() const override { return false; }

    bool checkSwitchBank(uInt16 address, uInt8) override;

    uInt16 hotspot() const override { return 0x06a0; }

    uInt16 getStartBank() const override { return 1; }

  private:
    // Previous Device's page access
    System::PageAccess myHotSpotPageAccess;

  private:
    // Following constructors and assignment operators not supported
    Cartridge0FA0() = delete;
    Cartridge0FA0(const Cartridge0FA0&) = delete;
    Cartridge0FA0(Cartridge0FA0&&) = delete;
    Cartridge0FA0& operator=(const Cartridge0FA0&) = delete;
    Cartridge0FA0& operator=(Cartridge0FA0&&) = delete;
};

#endif
