#include "GUI/Pages/Grid/GridPages.h"
#include "GUI/MCLEncoder.h"

MCLEncoder param1(GRID_WIDTH - 1, 0, 1);
MCLEncoder param2(GRID_LENGTH - 1, 0 , 1);
MCLEncoder param3(GRID_WIDTH + 1, 0, ENCODER_RES_GRID);
MCLEncoder param4(6, 1, ENCODER_RES_GRID);

GridPage grid_page(&param1, &param2, &param3, &param4);

MCLEncoder gridload_param1(1, 3, ENCODER_RES_PAT);
MCLExpEncoder gridio_param2(64, 1, ENCODER_RES_PAT);
MCLExpEncoder gridio_param4(64, 1, ENCODER_RES_PAT);

#if MCL_GRID_IO_PANEL
MCLEncoder gridload_fade(LOAD_FADE_MAX + 1, 0, ENCODER_RES_PAT);
#endif

#if !MCL_GRID_IO_PANEL
// AVR and TBD run Load and Save on the Grid page (GridPage::io_*).
MCLEncoder gridsave_param1(0, 2, ENCODER_RES_PAT);
MCLEncoder gridio_param3(0, 64, ENCODER_RES_PAT);
GridSavePage grid_save_page(&gridsave_param1, &gridio_param2, &gridio_param3,
                            &gridio_param4);
GridLoadPage grid_load_page(&gridload_param1, &gridio_param2, &gridio_param4,
                             &gridio_param4);
#endif

GridTrack slot;
