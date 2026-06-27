# CST3530

CST3530 touch driver for Tactility.

Used by the LilyGO T-Deck Pro Max, among others. The chip's I2C address depends on the
level of its interrupt pin at power-on (0x58 default, 0x5D if INT was low, 0x14 if INT
was high); pass an explicit address to `Cst3530Touch::Configuration` if the board doesn't
use the default.
