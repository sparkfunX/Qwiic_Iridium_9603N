@echo Programming the SparkX Qwiic Iridium. If this looks incorrect, abort and retry.
@pause
:loop
@echo Flashing bootloader and  firmware...
@avrdude -C avrdude.conf -pt841 -cusbtiny -e -Uefuse:w:0xFF:m -Uhfuse:w:0xDF:m -Ulfuse:w:0xC2:m -Uflash:w:Qwiic_Iridium_9603N_ATtiny841.ino.hex:i
@echo Done programming! Move on to the next board.
@pause
goto loop
