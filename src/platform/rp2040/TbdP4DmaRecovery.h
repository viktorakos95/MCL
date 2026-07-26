#pragma once

#ifdef PLATFORM_TBD

#include <hardware/dma.h>
#include <hardware/spi.h>

// DaDa_SPI v1.0.5 does not expose its claimed DMA channel numbers or an abort
// operation.  Identify the two channels by their fixed SPI data-register end
// point, stop both before aborting (required by RP2350-E5), then reset the SPI
// peripheral to clear any stale FIFO/error state.  DaDa_SPI::StartDMA()
// reconfigures the same claimed channels on the next transfer.
inline void tbd_p4_recover_spi_dma(spi_inst_t *spi, uint32_t speed) {
  const uintptr_t data_register = (uintptr_t)&spi_get_hw(spi)->dr;
  uint32_t channel_mask = 0;

  for (uint channel = 0; channel < NUM_DMA_CHANNELS; ++channel) {
    dma_channel_hw_t *hw = dma_channel_hw_addr(channel);
    if (hw->read_addr == data_register || hw->write_addr == data_register) {
      channel_mask |= 1u << channel;
    }
  }

  // Clear EN on every participating channel before issuing ABORT.  Besides
  // satisfying the RP2350 erratum this prevents either half of the paired
  // transfer from retriggering while the other is being stopped.
  for (uint channel = 0; channel < NUM_DMA_CHANNELS; ++channel) {
    if (channel_mask & (1u << channel)) {
      hw_clear_bits(&dma_channel_hw_addr(channel)->al1_ctrl,
                    DMA_CH0_CTRL_TRIG_EN_BITS);
    }
  }

  if (channel_mask != 0) {
    dma_hw->abort = channel_mask;
    for (uint channel = 0; channel < NUM_DMA_CHANNELS; ++channel) {
      if (channel_mask & (1u << channel)) {
        while (dma_channel_is_busy(channel)) tight_loop_contents();
      }
    }
  }

  spi_deinit(spi);
  spi_init(spi, speed);
  spi_set_format(spi, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
}

#endif // PLATFORM_TBD
