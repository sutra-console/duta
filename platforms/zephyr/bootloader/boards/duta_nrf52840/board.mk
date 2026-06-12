MCU_SUB_VARIANT = nrf52840

# Duta extension: exit-to-app on a 1200-baud CDC touch (see duta_exit.c). C_SRC
# accumulates across the Makefile, so appending here (board.mk is -included
# early) folds our file into the build for this board only.
C_SRC += src/boards/$(BOARD)/duta_exit.c
