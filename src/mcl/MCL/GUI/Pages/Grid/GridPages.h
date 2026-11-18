/* Copyright Justin Mammarella jmamma@gmail.com 2018 */

#ifndef GRIDPAGES_H__
#define GRIDPAGES_H__


#define ENCODER_RES_GRID 1
#define ENCODER_RES_PAT 2

#include "GUI/Pages/Grid/GridPage.h"

#include "GUI/MCLEncoder.h"
#include "GUI/Pages/Grid/GridSavePage.h"
#include "GUI/Pages/Grid/GridLoadPage.h"
#include "Menu.h"
#include "MenuPage.h"
#include "Grid/GridLink.h"
#include "GridTrack.h"
#include "MCLSysConfig.h"

extern MCLEncoder param1;
extern MCLEncoder param2;
extern MCLEncoder param3;
extern MCLEncoder param4;

extern GridPage grid_page;

extern MCLEncoder gridload_param1;

extern MCLExpEncoder gridio_param2;
extern MCLEncoder gridio_param3;
extern MCLExpEncoder gridio_param4;
#if MCL_GRID_IO_PANEL
// FADE on the LOAD panel. Turning steps OUT, off, IN, OUTIN (-1 .. 2);
// turning with the knob held down sets the length (0 follows QUANT).
class MCLFadeEncoder : public MCLEncoder {
public:
  uint8_t length = 0;
  constexpr MCLFadeEncoder() : MCLEncoder(0, 0, ENCODER_RES_PAT) {
    min = -1;
    max = LOAD_FADE_OUTIN;
  }
  int update(encoder_t *enc) override;
};
extern MCLFadeEncoder gridload_fade;
#endif

extern GridSavePage grid_save_page;
extern GridLoadPage grid_load_page;

extern GridTrack slot;

#endif /* GRIDPAGES_H__ */
