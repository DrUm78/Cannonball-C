#include "globals.h"
#include "romloader.h"
#include "hwvideo/hwtiles.h"
#include "frontend/config.h"

#include <string.h>

/***************************************************************************
    Video Emulation: OutRun Tilemap Hardware.
    Based on MAME source code.

    Copyright Aaron Giles.
    All rights reserved.
***************************************************************************/

/*******************************************************************************************
 *
 *  System 16B-style tilemaps
 *
 *  16 total pages
 *  Column/rowscroll enabled via bits in text layer
 *  Alternate tilemap support
 *
 *  Tile format:
 *      Bits               Usage
 *      p------- --------  Tile priority versus sprites
 *      -??----- --------  Unknown
 *      ---ccccc cc------  Tile color palette
 *      ---nnnnn nnnnnnnn  Tile index
 *
 *  Text format:
 *      Bits               Usage
 *      p------- --------  Tile priority versus sprites
 *      -???---- --------  Unknown
 *      ----ccc- --------  Tile color palette
 *      -------n nnnnnnnn  Tile index
 *
 *  Alternate tile format:
 *      Bits               Usage
 *      p------- --------  Tile priority versus sprites
 *      -??----- --------  Unknown
 *      ----cccc ccc-----  Tile color palette
 *      ---nnnnn nnnnnnnn  Tile index
 *
 *  Alternate text format:
 *      Bits               Usage
 *      p------- --------  Tile priority versus sprites
 *      -???---- --------  Unknown
 *      -----ccc --------  Tile color palette
 *      -------- nnnnnnnn  Tile index
 *
 *  Text RAM:
 *      Offset   Bits               Usage
 *      E80      aaaabbbb ccccdddd  Foreground tilemap page select
 *      E82      aaaabbbb ccccdddd  Background tilemap page select
 *      E84      aaaabbbb ccccdddd  Alternate foreground tilemap page select
 *      E86      aaaabbbb ccccdddd  Alternate background tilemap page select
 *      E90      c------- --------  Foreground tilemap column scroll enable
 *               -------v vvvvvvvv  Foreground tilemap vertical scroll
 *      E92      c------- --------  Background tilemap column scroll enable
 *               -------v vvvvvvvv  Background tilemap vertical scroll
 *      E94      -------v vvvvvvvv  Alternate foreground tilemap vertical scroll
 *      E96      -------v vvvvvvvv  Alternate background tilemap vertical scroll
 *      E98      r------- --------  Foreground tilemap row scroll enable
 *               ------hh hhhhhhhh  Foreground tilemap horizontal scroll
 *      E9A      r------- --------  Background tilemap row scroll enable
 *               ------hh hhhhhhhh  Background tilemap horizontal scroll
 *      E9C      ------hh hhhhhhhh  Alternate foreground tilemap horizontal scroll
 *      E9E      ------hh hhhhhhhh  Alternate background tilemap horizontal scroll
 *      F16-F3F  -------- vvvvvvvv  Foreground tilemap per-16-pixel-column vertical scroll
 *      F56-F7F  -------- vvvvvvvv  Background tilemap per-16-pixel-column vertical scroll
 *      F80-FB7  a------- --------  Foreground tilemap per-8-pixel-row alternate tilemap enable
 *               -------h hhhhhhhh  Foreground tilemap per-8-pixel-row horizontal scroll
 *      FC0-FF7  a------- --------  Background tilemap per-8-pixel-row alternate tilemap enable
 *               -------h hhhhhhhh  Background tilemap per-8-pixel-row horizontal scroll
 *
 *******************************************************************************************/

uint8_t HWTiles_text_ram[0x1000]; // Text RAM
uint8_t HWTiles_tile_ram[0x10000]; // Tile RAM

int16_t HWTiles_x_clamp;
    
// S16 Width, ignoring widescreen related scaling.
uint16_t HWTiles_s16_width_noscale;

#define TILES_LENGTH 0x10000
uint32_t HWTiles_tiles[TILES_LENGTH];        // Converted tiles
uint32_t HWTiles_tiles_backup[TILES_LENGTH]; // Converted tiles (backup without patch)

uint16_t HWTiles_page[4];
uint16_t HWTiles_scroll_x[4];
uint16_t HWTiles_scroll_y[4];

uint8_t HWTiles_tile_banks[2] = { 0, 1 };

static const uint16_t NUM_TILES = 0x2000; // Length of graphic rom / 24
static const uint16_t TILEMAP_COLOUR_OFFSET = 0x1c00;
    
void (*HWTiles_render8x8_tile_mask)(
    uint16_t *buf,
    uint16_t nTileNumber, 
    uint16_t StartX, 
    uint16_t StartY, 
    uint16_t nTilePalette, 
    uint16_t nColourDepth, 
    uint16_t nMaskColour, 
    uint16_t nPaletteOffset); 
        
void (*HWTiles_render8x8_tile_mask_clip)(
    uint16_t *buf,
    uint16_t nTileNumber, 
    int16_t StartX, 
    int16_t StartY, 
    uint16_t nTilePalette, 
    uint16_t nColourDepth, 
    uint16_t nMaskColour, 
    uint16_t nPaletteOffset); 
        
void HWTiles_render8x8_tile_mask_lores(
    uint16_t *buf,
    uint16_t nTileNumber, 
    uint16_t StartX, 
    uint16_t StartY, 
    uint16_t nTilePalette, 
    uint16_t nColourDepth, 
    uint16_t nMaskColour, 
    uint16_t nPaletteOffset); 

void HWTiles_render8x8_tile_mask_clip_lores(
    uint16_t *buf,
    uint16_t nTileNumber, 
    int16_t StartX, 
    int16_t StartY, 
    uint16_t nTilePalette, 
    uint16_t nColourDepth, 
    uint16_t nMaskColour, 
    uint16_t nPaletteOffset);
        
void HWTiles_render8x8_tile_mask_hires(
    uint16_t *buf,
    uint16_t nTileNumber, 
    uint16_t StartX, 
    uint16_t StartY, 
    uint16_t nTilePalette, 
    uint16_t nColourDepth, 
    uint16_t nMaskColour, 
    uint16_t nPaletteOffset); 
        
void HWTiles_render8x8_tile_mask_clip_hires(
    uint16_t *buf,
    uint16_t nTileNumber, 
    int16_t StartX, 
    int16_t StartY, 
    uint16_t nTilePalette, 
    uint16_t nColourDepth, 
    uint16_t nMaskColour, 
    uint16_t nPaletteOffset);
        
 void HWTiles_set_pixel_x4(uint16_t *buf, uint32_t data);

void HWTiles_Create(void)
{
    int i;
    for (i = 0; i < 2; i++)
        HWTiles_tile_banks[i] = i;

    HWTiles_set_x_clamp(HWTILES_CENTRE);
}

void HWTiles_Destroy(void)
{

}

// Convert S16 tiles to a more useable format
void HWTiles_init(uint8_t* src_tiles, const Boolean hires)
{
    int i;
    int ii;
    if (src_tiles)
    {
        for (i = 0; i < TILES_LENGTH; i++)
        {
            uint8_t p0 = src_tiles[i];
            uint8_t p1 = src_tiles[i + 0x10000];
            uint8_t p2 = src_tiles[i + 0x20000];

            uint32_t val = 0;

            for (ii = 0; ii < 8; ii++) 
            {
                uint8_t bit = 7 - ii;
                uint8_t pix = ((((p0 >> bit)) & 1) | (((p1 >> bit) << 1) & 2) | (((p2 >> bit) << 2) & 4));
                val = (val << 4) | pix;
            }
            HWTiles_tiles[i] = val; // Store converted value
        }
        memcpy(HWTiles_tiles_backup, HWTiles_tiles, TILES_LENGTH * sizeof(uint32_t));
    }
    
    if (hires)
    {
        HWTiles_s16_width_noscale = Config_s16_width >> 1;
        HWTiles_render8x8_tile_mask      = &HWTiles_render8x8_tile_mask_hires;
        HWTiles_render8x8_tile_mask_clip = &HWTiles_render8x8_tile_mask_clip_hires;
    }
    else
    {
        HWTiles_s16_width_noscale = Config_s16_width;
        HWTiles_render8x8_tile_mask      = &HWTiles_render8x8_tile_mask_lores;
        HWTiles_render8x8_tile_mask_clip = &HWTiles_render8x8_tile_mask_clip_lores;
    }
}

void HWTiles_patch_tiles(RomLoader* patch)
{
    memcpy(HWTiles_tiles_backup, HWTiles_tiles, TILES_LENGTH * sizeof(uint32_t));

    uint32_t i;

    for (i = 0; i < patch->length;)
    {
        uint32_t tile_index =         RomLoader_read16IncP(patch, &i) << 3;
        HWTiles_tiles[tile_index++] = RomLoader_read32IncP(patch, &i);
        HWTiles_tiles[tile_index++] = RomLoader_read32IncP(patch, &i);
        HWTiles_tiles[tile_index++] = RomLoader_read32IncP(patch, &i);
        HWTiles_tiles[tile_index++] = RomLoader_read32IncP(patch, &i);
        HWTiles_tiles[tile_index++] = RomLoader_read32IncP(patch, &i);
        HWTiles_tiles[tile_index++] = RomLoader_read32IncP(patch, &i);
        HWTiles_tiles[tile_index++] = RomLoader_read32IncP(patch, &i);
        HWTiles_tiles[tile_index++] = RomLoader_read32IncP(patch, &i);
    }
}

void HWTiles_restore_tiles()
{
    memcpy(HWTiles_tiles, HWTiles_tiles_backup, TILES_LENGTH * sizeof(uint32_t));
}

// Set Tilemap X Clamp
//
// This is used for the widescreen mode, in order to clamp the tilemap to
// a location of the screen. 
//
// In-Game we must clamp right to avoid page scrolling issues.
//
// The clamp will always be 192 for the non-widescreen mode.
void HWTiles_set_x_clamp(const uint16_t props)
{
    if (props == HWTILES_LEFT)
    {
        HWTiles_x_clamp = 192;
    }
    else if (props == HWTILES_RIGHT)
    {
        HWTiles_x_clamp = (512 - HWTiles_s16_width_noscale);
    }
    else if (props == HWTILES_CENTRE)
    {
        HWTiles_x_clamp = 192 - Config_s16_x_off;
    }
}

void HWTiles_update_tile_values()
{
    int i;
    for (i = 0; i < 4; i++)
    {
        HWTiles_page[i] = ((HWTiles_text_ram[0xe80 + (i * 2) + 0] << 8) | HWTiles_text_ram[0xe80 + (i * 2) + 1]);

        HWTiles_scroll_x[i] = ((HWTiles_text_ram[0xe98 + (i * 2) + 0] << 8) | HWTiles_text_ram[0xe98 + (i * 2) + 1]);
        HWTiles_scroll_y[i] = ((HWTiles_text_ram[0xe90 + (i * 2) + 0] << 8) | HWTiles_text_ram[0xe90 + (i * 2) + 1]);
    }
}

// A quick and dirty debug function to display the contents of tile memory.
void HWTiles_render_all_tiles(uint16_t* buf)
{
    uint32_t Code = 0, Colour = 5, x, y;
    for (y = 0; y < 224; y += 8) 
    {
        for (x = 0; x < 320; x += 8) 
        {
            HWTiles_render8x8_tile_mask(buf, Code, x, y, Colour, 3, 0, TILEMAP_COLOUR_OFFSET);
            Code++;
        }
    }
}

void HWTiles_render_tile_layer(uint16_t* buf, uint8_t page_index, uint8_t priority_draw)
{
    int my, mx;
    int16_t Colour, x, y, Priority = 0;

    uint16_t ActPage = 0;
    uint16_t EffPage = HWTiles_page[page_index];
    uint16_t xScroll = HWTiles_scroll_x[page_index];
    uint16_t yScroll = HWTiles_scroll_y[page_index];

    // Need to support this at each row/column
    if ((xScroll & 0x8000) != 0)
        xScroll = (HWTiles_text_ram[0xf80 + (0x40 * page_index) + 0] << 8) | HWTiles_text_ram[0xf80 + (0x40 * page_index) + 1];
    if ((yScroll & 0x8000) != 0)
        yScroll = (HWTiles_text_ram[0xf16 + (0x40 * page_index) + 0] << 8) | HWTiles_text_ram[0xf16 + (0x40 * page_index) + 1];

    for (my = 0; my < 64; my++) 
    {
        for (mx = 0; mx < 128; mx++) 
        {
            if (my < 32 && mx < 64)                    // top left
                ActPage = (EffPage >> 0) & 0x0f;
            if (my < 32 && mx >= 64)                   // top right
                ActPage = (EffPage >> 4) & 0x0f;
            if (my >= 32 && mx < 64)                   // bottom left
                ActPage = (EffPage >> 8) & 0x0f;
            if (my >= 32 && mx >= 64)                  // bottom right page
                ActPage = (EffPage >> 12) & 0x0f;

            uint32_t TileIndex = 64 * 32 * 2 * ActPage + ((2 * 64 * my) & 0xfff) + ((2 * mx) & 0x7f);

            uint16_t Data = (HWTiles_tile_ram[TileIndex + 0] << 8) | HWTiles_tile_ram[TileIndex + 1];

            Priority = (Data >> 15) & 1;

            if (Priority == priority_draw) 
            {
                uint32_t Code = Data & 0x1fff;
                Code = HWTiles_tile_banks[Code / 0x1000] * 0x1000 + Code % 0x1000;
                Code &= (NUM_TILES - 1);

                if (Code == 0) continue;

                Colour = (Data >> 6) & 0x7f;

                x = 8 * mx;
                y = 8 * my;

                // We take into account the internal screen resolution here
                // to account for widescreen mode.
                x -= (HWTiles_x_clamp - xScroll) & 0x3ff;

                if (x < -HWTiles_x_clamp)
                    x += 1024;

                y -= yScroll & 0x1ff;

                if (y < -288)
                    y += 512;

                uint16_t ColourOff = TILEMAP_COLOUR_OFFSET;
                if (Colour >= 0x20)
					ColourOff = 0x100 | TILEMAP_COLOUR_OFFSET;
                if (Colour >= 0x40)
					ColourOff = 0x200 | TILEMAP_COLOUR_OFFSET;
                if (Colour >= 0x60)
					ColourOff = 0x300 | TILEMAP_COLOUR_OFFSET;

                if (x > 7 && x < (HWTiles_s16_width_noscale - 8) && y > 7 && y <= (S16_HEIGHT - 8))
                    HWTiles_render8x8_tile_mask(buf, Code, x, y, Colour, 3, 0, ColourOff);
                else if (x > -8 && x < HWTiles_s16_width_noscale && y > -8 && y < S16_HEIGHT)
					HWTiles_render8x8_tile_mask_clip(buf, Code, x, y, Colour, 3, 0, ColourOff);
            } // end priority check
        }
    } // end for loop
}

void HWTiles_render_text_layer(uint16_t* buf, uint8_t priority_draw)
{
    uint16_t mx, my, Code, Colour, x, y, Priority, TileIndex = 0;

    for (my = 0; my < 32; my++) 
    {
        for (mx = 0; mx < 64; mx++) 
        {
            Code = (HWTiles_text_ram[TileIndex + 0] << 8) | HWTiles_text_ram[TileIndex + 1];
            Priority = (Code >> 15) & 1;

            if (Priority == priority_draw) 
            {
                Colour = (Code >> 9) & 0x07;
                Code &= 0x1ff;
                Code += HWTiles_tile_banks[0] * 0x1000;
                Code &= (NUM_TILES - 1);

                if (Code != 0) 
                {
                    x = 8 * mx;
                    y = 8 * my;

                    x -= 192;

                    // We also adjust the text layer for wide-screen below. But don't allow painting in the 
                    // wide-screen areas to avoid graphical glitches.
                    if (x > 7 && x < (HWTiles_s16_width_noscale - 8) && y > 7 && y <= (S16_HEIGHT - 8))
                        HWTiles_render8x8_tile_mask(buf, Code, x + Config_s16_x_off, y, Colour, 3, 0, TILEMAP_COLOUR_OFFSET);
                    else if (x > -8 && x < HWTiles_s16_width_noscale && y >= 0 && y < S16_HEIGHT) 
                        HWTiles_render8x8_tile_mask_clip(buf, Code, x + Config_s16_x_off, y, Colour, 3, 0, TILEMAP_COLOUR_OFFSET);
                }
            }
            TileIndex += 2;
        }
    }
}

void HWTiles_render8x8_tile_mask_lores(
    
    uint16_t *buf,
    uint16_t nTileNumber, 
    uint16_t StartX, 
    uint16_t StartY, 
    uint16_t nTilePalette, 
    uint16_t nColourDepth, 
    uint16_t nMaskColour, 
    uint16_t nPaletteOffset) 
{
    int y;
    uint32_t nPalette = (nTilePalette << nColourDepth) | nMaskColour;
    uint32_t* pTileData = HWTiles_tiles + (nTileNumber << 3);
    buf += (StartY * Config_s16_width) + StartX;

    for (y = 0; y < 8; y++) 
    {
        uint32_t p0 = *pTileData;

        if (p0 != nMaskColour) 
        {
            uint32_t c7 = p0 & 0xf;
            uint32_t c6 = (p0 >> 4) & 0xf;
            uint32_t c5 = (p0 >> 8) & 0xf;
            uint32_t c4 = (p0 >> 12) & 0xf;
            uint32_t c3 = (p0 >> 16) & 0xf;
            uint32_t c2 = (p0 >> 20) & 0xf;
            uint32_t c1 = (p0 >> 24) & 0xf;
            uint32_t c0 = (p0 >> 28);

            if (c0) buf[0] = nPalette + c0;
            if (c1) buf[1] = nPalette + c1;
            if (c2) buf[2] = nPalette + c2;
            if (c3) buf[3] = nPalette + c3;
            if (c4) buf[4] = nPalette + c4;
            if (c5) buf[5] = nPalette + c5;
            if (c6) buf[6] = nPalette + c6;
            if (c7) buf[7] = nPalette + c7;
        }
        buf += Config_s16_width;
        pTileData++;
    }
}

void HWTiles_render8x8_tile_mask_clip_lores(
    uint16_t *buf,
    uint16_t nTileNumber, 
    int16_t StartX, 
    int16_t StartY, 
    uint16_t nTilePalette, 
    uint16_t nColourDepth, 
    uint16_t nMaskColour, 
    uint16_t nPaletteOffset) 
{
    int y;
    uint32_t nPalette = (nTilePalette << nColourDepth) | nMaskColour;
    uint32_t* pTileData = HWTiles_tiles + (nTileNumber << 3);
    buf += (StartY * Config_s16_width) + StartX;

    for (y = 0; y < 8; y++) 
    {
        if ((StartY + y) >= 0 && (StartY + y) < S16_HEIGHT) 
        {
            uint32_t p0 = *pTileData;

            if (p0 != nMaskColour) 
            {
                uint32_t c7 = p0 & 0xf;
                uint32_t c6 = (p0 >> 4) & 0xf;
                uint32_t c5 = (p0 >> 8) & 0xf;
                uint32_t c4 = (p0 >> 12) & 0xf;
                uint32_t c3 = (p0 >> 16) & 0xf;
                uint32_t c2 = (p0 >> 20) & 0xf;
                uint32_t c1 = (p0 >> 24) & 0xf;
                uint32_t c0 = (p0 >> 28);

                if (c0 && 0 + StartX >= 0 && 0 + StartX < Config_s16_width) buf[0] = nPalette + c0;
                if (c1 && 1 + StartX >= 0 && 1 + StartX < Config_s16_width) buf[1] = nPalette + c1;
                if (c2 && 2 + StartX >= 0 && 2 + StartX < Config_s16_width) buf[2] = nPalette + c2;
                if (c3 && 3 + StartX >= 0 && 3 + StartX < Config_s16_width) buf[3] = nPalette + c3;
                if (c4 && 4 + StartX >= 0 && 4 + StartX < Config_s16_width) buf[4] = nPalette + c4;
                if (c5 && 5 + StartX >= 0 && 5 + StartX < Config_s16_width) buf[5] = nPalette + c5;
                if (c6 && 6 + StartX >= 0 && 6 + StartX < Config_s16_width) buf[6] = nPalette + c6;
                if (c7 && 7 + StartX >= 0 && 7 + StartX < Config_s16_width) buf[7] = nPalette + c7;
            }
        }
        buf += Config_s16_width;
        pTileData++;
    }
}

// ------------------------------------------------------------------------------------------------
// Additional routines for Hi-Res Mode.
// Note that the tilemaps are displayed at the same resolution, we just want everything to be
// proportional.
// ------------------------------------------------------------------------------------------------
void HWTiles_render8x8_tile_mask_hires(
    uint16_t *buf,
    uint16_t nTileNumber, 
    uint16_t StartX, 
    uint16_t StartY, 
    uint16_t nTilePalette, 
    uint16_t nColourDepth, 
    uint16_t nMaskColour, 
    uint16_t nPaletteOffset) 
{
    int y;
    uint32_t nPalette = (nTilePalette << nColourDepth) | nMaskColour;
    uint32_t* pTileData = HWTiles_tiles + (nTileNumber << 3);
    buf += ((StartY << 1) * Config_s16_width) + (StartX << 1);
    
    for (y = 0; y < 8; y++) 
    {
        uint32_t p0 = *pTileData;

        if (p0 != nMaskColour) 
        {
            uint32_t c7 = p0 & 0xf;
            uint32_t c6 = (p0 >> 4) & 0xf;
            uint32_t c5 = (p0 >> 8) & 0xf;
            uint32_t c4 = (p0 >> 12) & 0xf;
            uint32_t c3 = (p0 >> 16) & 0xf;
            uint32_t c2 = (p0 >> 20) & 0xf;
            uint32_t c1 = (p0 >> 24) & 0xf;
            uint32_t c0 = (p0 >> 28);

            if (c0) HWTiles_set_pixel_x4(&buf[0],  nPalette + c0);
            if (c1) HWTiles_set_pixel_x4(&buf[2],  nPalette + c1);
            if (c2) HWTiles_set_pixel_x4(&buf[4],  nPalette + c2);
            if (c3) HWTiles_set_pixel_x4(&buf[6],  nPalette + c3);
            if (c4) HWTiles_set_pixel_x4(&buf[8],  nPalette + c4);
            if (c5) HWTiles_set_pixel_x4(&buf[10], nPalette + c5);
            if (c6) HWTiles_set_pixel_x4(&buf[12], nPalette + c6);
            if (c7) HWTiles_set_pixel_x4(&buf[14], nPalette + c7);
        }
        buf += (Config_s16_width << 1);
        pTileData++;
    }
}

void HWTiles_render8x8_tile_mask_clip_hires(
    uint16_t *buf,
    uint16_t nTileNumber, 
    int16_t StartX, 
    int16_t StartY, 
    uint16_t nTilePalette, 
    uint16_t nColourDepth, 
    uint16_t nMaskColour, 
    uint16_t nPaletteOffset) 
{
    int y;
    uint32_t nPalette = (nTilePalette << nColourDepth) | nMaskColour;
    uint32_t* pTileData = HWTiles_tiles + (nTileNumber << 3);
    buf += ((StartY << 1) * Config_s16_width) + (StartX << 1);

    for (y = 0; y < 8; y++) 
    {
        if ((StartY + y) >= 0 && (StartY + y) < S16_HEIGHT) 
        {
            uint32_t p0 = *pTileData;

            if (p0 != nMaskColour) 
            {
                uint32_t c7 = p0 & 0xf;
                uint32_t c6 = (p0 >> 4) & 0xf;
                uint32_t c5 = (p0 >> 8) & 0xf;
                uint32_t c4 = (p0 >> 12) & 0xf;
                uint32_t c3 = (p0 >> 16) & 0xf;
                uint32_t c2 = (p0 >> 20) & 0xf;
                uint32_t c1 = (p0 >> 24) & 0xf;
                uint32_t c0 = (p0 >> 28);

                if (c0 && 0 + StartX >= 0 && 0 + StartX < HWTiles_s16_width_noscale) HWTiles_set_pixel_x4(&buf[0],  nPalette + c0);
                if (c1 && 1 + StartX >= 0 && 1 + StartX < HWTiles_s16_width_noscale) HWTiles_set_pixel_x4(&buf[2],  nPalette + c1);
                if (c2 && 2 + StartX >= 0 && 2 + StartX < HWTiles_s16_width_noscale) HWTiles_set_pixel_x4(&buf[4],  nPalette + c2);
                if (c3 && 3 + StartX >= 0 && 3 + StartX < HWTiles_s16_width_noscale) HWTiles_set_pixel_x4(&buf[6],  nPalette + c3);
                if (c4 && 4 + StartX >= 0 && 4 + StartX < HWTiles_s16_width_noscale) HWTiles_set_pixel_x4(&buf[8],  nPalette + c4);
                if (c5 && 5 + StartX >= 0 && 5 + StartX < HWTiles_s16_width_noscale) HWTiles_set_pixel_x4(&buf[10], nPalette + c5);
                if (c6 && 6 + StartX >= 0 && 6 + StartX < HWTiles_s16_width_noscale) HWTiles_set_pixel_x4(&buf[12], nPalette + c6);
                if (c7 && 7 + StartX >= 0 && 7 + StartX < HWTiles_s16_width_noscale) HWTiles_set_pixel_x4(&buf[14], nPalette + c7);
            }
        }
        buf += (Config_s16_width << 1);
        pTileData++;
    }
}

// Hires Mode: Set 4 pixels instead of one.
void HWTiles_set_pixel_x4(uint16_t *buf, uint32_t data)
{
    buf[0] = buf[1] = buf[0  + Config_s16_width] = buf[1 + Config_s16_width] = data;
}
