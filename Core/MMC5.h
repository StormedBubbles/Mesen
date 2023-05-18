#pragma once
#include "stdafx.h"
#include "BaseMapper.h"
#include "PPU.h"
#include "MMC5Audio.h"
#include "MMC5MemoryHandler.h"

class MMC5 : public BaseMapper
{
private:
	static constexpr int ExRamSize = 0x400;
	static constexpr uint8_t NtWorkRamIndex = 4;
	static constexpr uint8_t NtEmptyIndex = 2;
	static constexpr uint8_t NtFillModeIndex = 3;

	unique_ptr<MMC5Audio> _audio;
	unique_ptr<MMC5MemoryHandler> _mmc5MemoryHandler;

	uint8_t _prgRamProtect1;
	uint8_t _prgRamProtect2;

	uint8_t _fillModeTile;
	uint8_t _fillModeColor;

	bool _verticalSplitEnabled;
	bool _verticalSplitRightSide;
	uint8_t _verticalSplitDelimiterTile;
	uint8_t _verticalSplitScroll;
	uint8_t _verticalSplitBank;

	bool _splitInSplitRegion;
	uint32_t _splitVerticalScroll;
	uint32_t _splitTile;
	int32_t _splitTileNumber;

	uint8_t _multiplierValue1;
	uint8_t _multiplierValue2;

	uint8_t _nametableMapping;
	uint8_t _extendedRamMode;

	//Extended attribute mode fields (used when _extendedRamMode == 1)
	uint16_t _exAttributeLastNametableFetch;
	int8_t _exAttrLastFetchCounter;
	uint8_t _exAttrSelectedChrBank;

	uint8_t _prgMode;
	uint8_t _prgBanks[5];

	//CHR-related fields
	uint8_t _chrMode;
	uint8_t _chrUpperBits;
	uint16_t _chrBanks[12];
	uint16_t _lastChrReg;
	bool _prevChrA;

	//IRQ counter related fields
	uint8_t _irqCounterTarget;
	bool _irqEnabled;
	uint8_t _scanlineCounter;
	bool _irqPending;

	bool _needInFrame;
	bool _ppuInFrame;
	uint8_t _ppuIdleCounter;
	uint16_t _lastPpuReadAddr;
	uint8_t _ntReadCounter;

	void SwitchPrgBank(uint16_t reg, uint8_t value)
	{
		_prgBanks[reg - 0x5113] = value;
		UpdatePrgBanks();
	}

	void GetCpuBankInfo(uint16_t reg, uint8_t &bankNumber, PrgMemoryType &memoryType, uint8_t &accessType)
	{
		bankNumber = _prgBanks[reg-0x5113];
		memoryType = PrgMemoryType::PrgRom;
		if((((bankNumber & 0x80) == 0x00) && reg != 0x5117) || reg == 0x5113) {
			bankNumber &= 0x07;
			accessType = MemoryAccessType::Read;
			if(_prgRamProtect1 == 0x02 && _prgRamProtect2 == 0x01) {
				accessType |= MemoryAccessType::Write;
			}

			// WRAM/SRAM mirroring logic (only supports existing/known licensed MMC5 boards)
			//            Bank number
			//            0 1 2 3 4 5 6 7
			// --------------------------
			// None     : - - - - - - - -
			// 1x 8kb   : 0 0 0 0 - - - -
			// 2x 8kb   : 0 0 0 0 1 1 1 1
			// 1x 32kb  : 0 1 2 3 - - - -
			int32_t realWorkRamSize = _workRamSize - (HasBattery() ? 0 : MMC5::ExRamSize);
			int32_t realSaveRamSize = _saveRamSize - (!HasBattery() ? 0 : MMC5::ExRamSize);
			if(IsNes20() || _romInfo.IsInDatabase) {
				memoryType = PrgMemoryType::WorkRam;
				if(HasBattery() && (bankNumber <= 3 || realSaveRamSize > 0x2000)) {
					memoryType = PrgMemoryType::SaveRam;
				}

				if(realSaveRamSize + realWorkRamSize != 0x4000 && bankNumber >= 4) {
					//When not 2x 8kb (=16kb), banks 4/5/6/7 select the empty socket and return open bus
					accessType = MemoryAccessType::NoAccess;
				}
			} else {
				memoryType = HasBattery() ? PrgMemoryType::SaveRam : PrgMemoryType::WorkRam;
			}			

			if(memoryType == PrgMemoryType::WorkRam) {
				//Properly mirror work ram (by ignoring the extra 1kb ExRAM section)
				bankNumber &= (realWorkRamSize / 0x2000) - 1;
				if(_workRamSize == MMC5::ExRamSize) {
					accessType = MemoryAccessType::NoAccess;
				}
			} else if(memoryType == PrgMemoryType::SaveRam) {
				//Properly mirror work ram (by ignoring the extra 1kb ExRAM section)
				bankNumber &= (realSaveRamSize / 0x2000) - 1;
				if(_saveRamSize == MMC5::ExRamSize) {
					accessType = MemoryAccessType::NoAccess;
				}
			}
		} else {
			accessType = MemoryAccessType::Read;
			bankNumber &= 0x7F;
		}
	}

	void UpdatePrgBanks()
	{
		uint8_t value;
		PrgMemoryType memoryType;
		uint8_t accessType;

		GetCpuBankInfo(0x5113, value, memoryType, accessType);
		SetCpuMemoryMapping(0x6000, 0x7FFF, value, memoryType, accessType);

		//PRG Bank 0
		//Mode 0,1,2 - Ignored
		//Mode 3 - Select an 8KB PRG bank at $8000-$9FFF
		if(_prgMode == 3) {
			GetCpuBankInfo(0x5114, value, memoryType, accessType);
			SetCpuMemoryMapping(0x8000, 0x9FFF, value, memoryType, accessType);
		}

		//PRG Bank 1
		//Mode 0 - Ignored
		//Mode 1,2 - Select a 16KB PRG bank at $8000-$BFFF (ignore bottom bit)
		//Mode 3 - Select an 8KB PRG bank at $A000-$BFFF
		GetCpuBankInfo(0x5115, value, memoryType, accessType);
		if(_prgMode == 1 || _prgMode == 2) {
			SetCpuMemoryMapping(0x8000, 0xBFFF, value & 0xFE, memoryType, accessType);
		} else if(_prgMode == 3) {
			SetCpuMemoryMapping(0xA000, 0xBFFF, value, memoryType, accessType);
		}

		//Mode 0,1 - Ignored
		//Mode 2,3 - Select an 8KB PRG bank at $C000-$DFFF
		if(_prgMode == 2 || _prgMode == 3) {
			GetCpuBankInfo(0x5116, value, memoryType, accessType);
			SetCpuMemoryMapping(0xC000, 0xDFFF, value, memoryType, accessType);
		}

		//Mode 0 - Select a 32KB PRG ROM bank at $8000-$FFFF (ignore bottom 2 bits)
		//Mode 1 - Select a 16KB PRG ROM bank at $C000-$FFFF (ignore bottom bit)
		//Mode 2,3 - Select an 8KB PRG ROM bank at $E000-$FFFF
		GetCpuBankInfo(0x5117, value, memoryType, accessType);
		if(_prgMode == 0) {
			SetCpuMemoryMapping(0x8000, 0xFFFF, value & 0x7C, memoryType, accessType);
		} else if(_prgMode == 1) {
			SetCpuMemoryMapping(0xC000, 0xFFFF, value & 0x7E, memoryType, accessType);
		} else if(_prgMode == 2 || _prgMode == 3) {
			SetCpuMemoryMapping(0xE000, 0xFFFF, value & 0x7F, memoryType, accessType);
		}
	}

	void SwitchChrBank(uint16_t reg, uint8_t value)
	{
		uint16_t newValue = value | (_chrUpperBits << 8);
		if(newValue != _chrBanks[reg - 0x5120] || _lastChrReg != reg) {
			_chrBanks[reg - 0x5120] = newValue;
			_lastChrReg = reg;
			UpdateChrBanks(true);
		}
	}

	void UpdateChrBanks(bool forceUpdate)
	{
		bool largeSprites = (_mmc5MemoryHandler->GetReg(0x2000) & 0x20) != 0;

		if(!largeSprites) {
			//Using 8x8 sprites resets the last written to bank logic
			_lastChrReg = 0;
		}

		bool chrA = !largeSprites || (_splitTileNumber >= 32 && _splitTileNumber < 40) || (!_ppuInFrame && _lastChrReg <= 0x5127);
		if(!forceUpdate && chrA == _prevChrA) {
			return;
		}
		_prevChrA = chrA;

		if(_chrMode == 0) {
			SelectChrPage8x(0, _chrBanks[chrA ? 0x07 : 0x0B] << 3);
		} else if(_chrMode == 1) {
			SelectChrPage4x(0, _chrBanks[chrA ? 0x03 : 0x0B] << 2);
			SelectChrPage4x(1, _chrBanks[chrA ? 0x07 : 0x0B] << 2);
		} else if(_chrMode == 2) {
			SelectChrPage2x(0, _chrBanks[chrA ? 0x01 : 0x09] << 1);
			SelectChrPage2x(1, _chrBanks[chrA ? 0x03 : 0x0B] << 1);
			SelectChrPage2x(2, _chrBanks[chrA ? 0x05 : 0x09] << 1);
			SelectChrPage2x(3, _chrBanks[chrA ? 0x07 : 0x0B] << 1);
		} else if(_chrMode == 3) {
			SelectCHRPage(0, _chrBanks[chrA ? 0x00 : 0x08]);
			SelectCHRPage(1, _chrBanks[chrA ? 0x01 : 0x09]);
			SelectCHRPage(2, _chrBanks[chrA ? 0x02 : 0x0A]);
			SelectCHRPage(3, _chrBanks[chrA ? 0x03 : 0x0B]);
			SelectCHRPage(4, _chrBanks[chrA ? 0x04 : 0x08]);
			SelectCHRPage(5, _chrBanks[chrA ? 0x05 : 0x09]);
			SelectCHRPage(6, _chrBanks[chrA ? 0x06 : 0x0A]);
			SelectCHRPage(7, _chrBanks[chrA ? 0x07 : 0x0B]);
		}
	}

	void ProcessCpuClock() override
	{
		_audio->Clock();

		if(_ppuIdleCounter) {
			_ppuIdleCounter--;
			if(_ppuIdleCounter == 0) {
				//"The "in-frame" flag is cleared when the PPU is no longer rendering. This is detected when 3 CPU cycles pass without a PPU read having occurred (PPU /RD has not been low during the last 3 M2 rises)."
				_ppuInFrame = false;
				UpdateChrBanks(true);
			}
		}
	}

	void SetNametableMapping(uint8_t value)
	{
		_nametableMapping = value;

		uint8_t nametables[4] = {
			0,  //"0 - On-board VRAM page 0"
			1,  //"1 - On-board VRAM page 1"
			_extendedRamMode <= 1 ? NtWorkRamIndex : NtEmptyIndex, //"2 - Internal Expansion RAM, only if the Extended RAM mode allows it ($5104 is 00/01); otherwise, the nametable will read as all zeros,"
			NtFillModeIndex //"3 - Fill-mode data"
		};

		for(int i = 0; i < 4; i++) {
			uint8_t nametableId = nametables[(value >> (i * 2)) & 0x03];
			if(nametableId == NtWorkRamIndex) {
				uint8_t* source = HasBattery() ? (_saveRam + (_saveRamSize - MMC5::ExRamSize)) : (_workRam + (_workRamSize - MMC5::ExRamSize));
				SetPpuMemoryMapping(0x2000+i*0x400, 0x2000+i*0x400+0x3FF, source, MemoryAccessType::ReadWrite);
			} else {
				SetNametable(i, nametableId);
			}
		}
	}

	void SetExtendedRamMode(uint8_t mode)
	{
		_extendedRamMode = mode;

		MemoryAccessType accessType;
		if(_extendedRamMode <= 1) {
			//"Mode 0/1 - Not readable (returns open bus), can only be written while the PPU is rendering (otherwise, 0 is written)"
			//See overridden WriteRam function for implementation
			accessType = MemoryAccessType::Write;
		} else if(_extendedRamMode == 2) {
			//"Mode 2 - Readable and writable"
			accessType = MemoryAccessType::ReadWrite;
		} else {
			//"Mode 3 - Read-only"
			accessType = MemoryAccessType::Read;
		}

		if(HasBattery()) {
			SetCpuMemoryMapping(0x5C00, 0x5FFF, PrgMemoryType::SaveRam, _saveRamSize - MMC5::ExRamSize, accessType);
		} else {
			SetCpuMemoryMapping(0x5C00, 0x5FFF, PrgMemoryType::WorkRam, _workRamSize - MMC5::ExRamSize, accessType);
		}

		SetNametableMapping(_nametableMapping);
	}

	void SetFillModeTile(uint8_t tile)
	{
		_fillModeTile = tile;
		memset(GetNametable(NtFillModeIndex), tile, 32 * 30); //32 tiles per row, 30 rows
	}

	void SetFillModeColor(uint8_t color)
	{
		_fillModeColor = color;
		uint8_t attributeByte = color | color << 2 | color << 4 | color << 6;
		memset(GetNametable(NtFillModeIndex) + 32 * 30, attributeByte, 64); //Attribute table is 64 bytes
	}

protected:
	virtual uint16_t GetPRGPageSize() override { return 0x2000; }
	virtual uint16_t GetCHRPageSize() override { return 0x400; }
	virtual uint16_t RegisterStartAddress() override { return 0x5000; }
	virtual uint16_t RegisterEndAddress() override { return 0x5206; }
	virtual uint32_t GetSaveRamPageSize() override { return 0x2000; }
	virtual uint32_t GetWorkRamPageSize() override { return 0x2000; }
	virtual bool ForceSaveRamSize() override { return true; }
	virtual bool ForceWorkRamSize() override { return true; }

	virtual uint32_t GetSaveRamSize() override
	{
		uint32_t size;
		if(IsNes20()) {
			size = _romInfo.NesHeader.GetSaveRamSize();
		} else if(_romInfo.IsInDatabase) {
			size = _romInfo.DatabaseInfo.SaveRamSize;
		} else {
			//Emulate as if a single 64k block of work/save ram existed
			size = _romInfo.HasBattery ? 0x10000 : 0;
		}

		if(HasBattery()) {
			//If there's a battery on the board, exram gets saved, too.
			size += MMC5::ExRamSize;
		}

		return size;
	}

	virtual uint32_t GetWorkRamSize() override
	{
		uint32_t size;
		if(IsNes20()) {
			size = _romInfo.NesHeader.GetWorkRamSize();
		} else if(_romInfo.IsInDatabase) {
			size = _romInfo.DatabaseInfo.WorkRamSize;
		} else {
			//Emulate as if a single 64k block of work/save ram existed (+ 1kb of ExRAM)
			size = (_romInfo.HasBattery ? 0 : 0x10000);
		}
		if(!HasBattery()) {
			size += MMC5::ExRamSize;
		}
		
		return size;
	}

	virtual bool AllowRegisterRead() override { return true; }

	virtual void InitMapper() override
	{
		AddRegisterRange(0xFFFA, 0xFFFB, MemoryOperation::Read);

		_audio.reset(new MMC5Audio(_console));
		
		//Override the 2000-2007 registers to catch all writes to the PPU registers (but not their mirrors)
		_mmc5MemoryHandler.reset(new MMC5MemoryHandler(_console.get()));

		_ppuIdleCounter = 0;
		_lastPpuReadAddr = 0;
		_ntReadCounter = 0;
		_prevChrA = false;

		_chrMode = 0;
		_prgRamProtect1 = 0;
		_prgRamProtect2 = 0;
		_extendedRamMode = 0;
		_nametableMapping = 0;
		_fillModeColor = 0;
		_fillModeTile = 0;
		_verticalSplitScroll = 0;
		_verticalSplitBank = 0;
		_verticalSplitEnabled = false;
		_verticalSplitDelimiterTile = 0;
		_verticalSplitRightSide = false;
		_multiplierValue1 = 0;
		_multiplierValue2 = 0;
		_chrUpperBits = 0;
		memset(_chrBanks, 0, sizeof(_chrBanks));
		_lastChrReg = 0;

		_exAttrLastFetchCounter = 0;
		_exAttributeLastNametableFetch = 0;
		_exAttrSelectedChrBank = 0;
		
		_irqPending = false;
		_irqCounterTarget = 0;
		_scanlineCounter = 0;
		_irqEnabled = false;
		_ppuInFrame = false;
		_needInFrame = false;

		_splitInSplitRegion = false;
		_splitVerticalScroll = 0;
		_splitTile = 0;
		_splitTileNumber = -1;

		memset(GetNametable(NtEmptyIndex), 0, BaseMapper::NametableSize);

		SetExtendedRamMode(0);

		//"Additionally, Romance of the 3 Kingdoms 2 seems to expect it to be in 8k PRG mode ($5100 = $03)."
		WriteRegister(0x5100, 0x03);

		//"Games seem to expect $5117 to be $FF on powerup (last PRG page swapped in)."
		WriteRegister(0x5117, 0xFF);

		UpdateChrBanks(true);
	}

	void Reset(bool softReset) override
	{
		_console->GetMemoryManager()->RegisterWriteHandler(_mmc5MemoryHandler.get(), 0x2000, 0x2007);
	}

	void StreamState(bool saving) override
	{
		BaseMapper::StreamState(saving);

		int16_t unusedPreviousScanline = 0;
		bool unusedSpriteFetch = false;
		bool unusedLargeSprites = false;

		ArrayInfo<uint8_t> prgBanks = { _prgBanks, 5 };
		ArrayInfo<uint16_t> chrBanks = { _chrBanks, 12 };
		SnapshotInfo audio{ _audio.get() };
		Stream(_prgRamProtect1, _prgRamProtect2, _fillModeTile, _fillModeColor, _verticalSplitEnabled, _verticalSplitRightSide,
				_verticalSplitDelimiterTile, _verticalSplitScroll, _verticalSplitBank, _multiplierValue1, _multiplierValue2,
				_nametableMapping, _extendedRamMode, _exAttributeLastNametableFetch, _exAttrLastFetchCounter, _exAttrSelectedChrBank, 
				_prgMode, prgBanks, _chrMode, _chrUpperBits, chrBanks, _lastChrReg, 
				unusedSpriteFetch, unusedLargeSprites, _irqCounterTarget, _irqEnabled, unusedPreviousScanline, _scanlineCounter, _irqPending, _ppuInFrame, audio,
				_splitInSplitRegion, _splitVerticalScroll, _splitTile, _splitTileNumber, _needInFrame);

		if(!saving) {
			UpdatePrgBanks();
			SetNametableMapping(_nametableMapping);
		}
	}

	virtual void WriteRAM(uint16_t addr, uint8_t value) override
	{
		if(addr >= 0x5C00 && addr <= 0x5FFF && _extendedRamMode <= 1 && !_ppuInFrame) {
			//Expansion RAM ($5C00-$5FFF, read/write)
			//Mode 0/1 - Not readable (returns open bus), can only be written while the PPU is rendering (otherwise, 0 is written)
			value = 0;
		}
		BaseMapper::WriteRAM(addr, value);
	}

	void DetectScanlineStart(uint16_t addr)
	{
		if(_ntReadCounter >= 2) {
			//After 3 identical NT reads, trigger IRQ when the following attribute byte is read
			if(!_ppuInFrame && !_needInFrame) {
				_needInFrame = true;
				_scanlineCounter = 0;
			} else {
				_scanlineCounter++;
				if(_irqCounterTarget == _scanlineCounter) {
					_irqPending = true;
					if(_irqEnabled) {
						_console->GetCpu()->SetIrqSource(IRQSource::External);
					}
				}
			}
			_splitTileNumber = 0;
			_ntReadCounter = 0;
		} else if(addr >= 0x2000 && addr <= 0x2FFF) {
			if(_lastPpuReadAddr == addr) {
				//Count consecutive identical reads
				_ntReadCounter++;
			} else {
				_ntReadCounter = 0;
			}
		} else {
			_ntReadCounter = 0;
		}
	}

	virtual uint8_t MapperReadVRAM(uint16_t addr, MemoryOperationType memoryOperationType) override
	{
		bool isNtFetch = addr >= 0x2000 && addr <= 0x2FFF && (addr & 0x3FF) < 0x3C0;
		if(isNtFetch) {
			//Nametable data, not an attribute fetch
			_splitInSplitRegion = false;
			_splitTileNumber++;

			if(_ppuInFrame) {
				UpdateChrBanks(false);
			} else if(_needInFrame) {
				_needInFrame = false;
				_ppuInFrame = true;
				UpdateChrBanks(false);
			}
		}
		DetectScanlineStart(addr);

		_ppuIdleCounter = 3;
		_lastPpuReadAddr = addr;

		if(_extendedRamMode <= 1 && _ppuInFrame) {
			if(_verticalSplitEnabled) {
				uint16_t verticalSplitScroll = (_verticalSplitScroll + _scanlineCounter) % 240;
				if(addr >= 0x2000) {
					if(isNtFetch) {
						uint8_t tileNumber = (_splitTileNumber + 2) % 42;
						if(tileNumber <= 32 && ((_verticalSplitRightSide && tileNumber >= _verticalSplitDelimiterTile) || (!_verticalSplitRightSide && tileNumber < _verticalSplitDelimiterTile))) {
							//Split region (for next 3 fetches, attribute + 2x tile data)
							_splitInSplitRegion = true;
							_splitTile = ((verticalSplitScroll & 0xF8) << 2) | tileNumber;
							return InternalReadRam(0x5C00 + _splitTile);
						} else {
							//Outside of split region (or sprite data), result can get modified by ex ram mode code below
							_splitInSplitRegion = false;
						}
					} else if(_splitInSplitRegion) {
						return InternalReadRam(0x5FC0 | ((_splitTile & 0x380) >> 4) | ((_splitTile & 0x1F) >> 2));
					}
				} else if(_splitInSplitRegion) {
					//CHR tile fetches for split region
					return _chrRom[(_verticalSplitBank % (GetCHRPageCount() / 4)) * 0x1000 + (((addr & ~0x07) | (verticalSplitScroll & 0x07)) & 0xFFF)];
				}
			}

			if(_extendedRamMode == 1 && (_splitTileNumber < 32 || _splitTileNumber >= 40)) {
				//"In Mode 1, nametable fetches are processed normally, and can come from CIRAM nametables, fill mode, or even Expansion RAM, but attribute fetches are replaced by data from Expansion RAM."
				//"Each byte of Expansion RAM is used to enhance the tile at the corresponding address in every nametable"

				//When fetching NT data, we set a flag and then alter the VRAM values read by the PPU on the following 3 cycles (palette, tile low/high byte)
				if(isNtFetch) {
					//Nametable fetches
					_exAttributeLastNametableFetch = addr & 0x03FF;
					_exAttrLastFetchCounter = 3;
				} else if(_exAttrLastFetchCounter > 0) {
					//Attribute fetches
					_exAttrLastFetchCounter--;
					switch(_exAttrLastFetchCounter) {
						case 2:
						{
							//PPU palette fetch
							//Check work ram (expansion ram) to see which tile/palette to use
							//Use InternalReadRam to bypass the fact that the ram is supposed to be write-only in mode 0/1
							uint8_t value = InternalReadRam(0x5C00 + _exAttributeLastNametableFetch);

							//"The pattern fetches ignore the standard CHR banking bits, and instead use the top two bits of $5130 and the bottom 6 bits from Expansion RAM to choose a 4KB bank to select the tile from."
							_exAttrSelectedChrBank = ((value & 0x3F) | (_chrUpperBits << 6)) % (_chrRomSize / 0x1000);

							//Return a byte containing the same palette 4 times - this allows the PPU to select the right palette no matter the shift value
							uint8_t palette = (value & 0xC0) >> 6;
							return palette | palette << 2 | palette << 4 | palette << 6;
						}

						case 1:
						case 0:
							//PPU tile data fetch (high byte & low byte)
							return _chrRom[_exAttrSelectedChrBank * 0x1000 + (addr & 0xFFF)];
					}
				}
			}
		}
		
		return InternalReadVRAM(addr);
	}

	void WriteRegister(uint16_t addr, uint8_t value) override
	{
		if(addr >= 0x5113 && addr <= 0x5117) {
			SwitchPrgBank(addr, value);
		} else if(addr >= 0x5120 && addr <= 0x512B) {
			SwitchChrBank(addr, value);
		} else {
			switch(addr) {
				case 0x5000: case 0x5001: case 0x5002: case 0x5003: case 0x5004: case 0x5005: case 0x5006: case 0x5007: case 0x5010: case 0x5011: case 0x5015:
					_audio->WriteRegister(addr, value);
					break;

				case 0x5100: _prgMode = value & 0x03; UpdatePrgBanks(); break;
				case 0x5101: _chrMode = value & 0x03; UpdateChrBanks(true); break;
				case 0x5102: _prgRamProtect1 = value & 0x03; UpdatePrgBanks(); break;
				case 0x5103: _prgRamProtect2 = value & 0x03; UpdatePrgBanks(); break;
				case 0x5104: SetExtendedRamMode(value & 0x03); break;
				case 0x5105: SetNametableMapping(value); break;
				case 0x5106: SetFillModeTile(value); break;
				case 0x5107: SetFillModeColor(value & 0x03); break;
				case 0x5130: _chrUpperBits = value & 0x03; break;
				case 0x5200: 
					_verticalSplitEnabled = (value & 0x80) == 0x80; 
					_verticalSplitRightSide = (value & 0x40) == 0x40; 
					_verticalSplitDelimiterTile = (value & 0x1F);
					break;
				case 0x5201: _verticalSplitScroll = value; break;
				case 0x5202: _verticalSplitBank = value; break;
				case 0x5203: _irqCounterTarget = value; break;
				case 0x5204: 
					_irqEnabled = (value & 0x80) == 0x80;
					if(!_irqEnabled) {
						_console->GetCpu()->ClearIrqSource(IRQSource::External);
					} else if(_irqEnabled && _irqPending) {
						_console->GetCpu()->SetIrqSource(IRQSource::External);
					}
					break;
				case 0x5205: _multiplierValue1 = value; break;
				case 0x5206: _multiplierValue2 = value; break;

				default:
					break;
			}
		}
	}

	uint8_t ReadRegister(uint16_t addr) override
	{
		switch(addr) {
			case 0x5010: case 0x5015: 
				return _audio->ReadRegister(addr);

			case 0x5204:
			{
				uint8_t value = (_ppuInFrame ? 0x40 : 0x00) | (_irqPending ? 0x80 : 0x00);
				_irqPending = false;
				_console->GetCpu()->ClearIrqSource(IRQSource::External);
				return value;
			}

			case 0x5205: return (_multiplierValue1*_multiplierValue2) & 0xFF;
			case 0x5206: return (_multiplierValue1*_multiplierValue2) >> 8;

			case 0xFFFA: 
			case 0xFFFB:
				_ppuInFrame = false;
				UpdateChrBanks(true);
				_lastPpuReadAddr = 0;
				_scanlineCounter = 0;
				_irqPending = false;
				_console->GetCpu()->ClearIrqSource(IRQSource::External);
				return DebugReadRAM(addr);
		}

		return _console->GetMemoryManager()->GetOpenBus();
	}

public:
	bool IsExtendedAttributes()
	{
		return _extendedRamMode == 1;
	}

	uint8_t GetExAttributeNtPalette(uint16_t ntAddr)
	{
		ntAddr &= 0x3FF;		
		uint8_t value = InternalReadRam(0x5C00 + ntAddr);
		return (value & 0xC0) >> 6;
	}

	uint32_t GetExAttributeAbsoluteTileAddr(uint16_t ntAddr, uint16_t chrAddr)
	{
		ntAddr &= 0x3FF;
		uint8_t value = InternalReadRam(0x5C00 + ntAddr);

		//"The pattern fetches ignore the standard CHR banking bits, and instead use the top two bits of $5130 and the bottom 6 bits from Expansion RAM to choose a 4KB bank to select the tile from."
		uint16_t chrBank = ((value & 0x3F) | (_chrUpperBits << 6)) % (_chrRomSize / 0x1000);

		return chrBank * 0x1000 + (chrAddr & 0xFFF);
	}

	uint8_t GetExAttributeTileData(uint16_t ntAddr, uint16_t chrAddr)
	{
		return _chrRom[GetExAttributeAbsoluteTileAddr(ntAddr, chrAddr)];
	}
};
