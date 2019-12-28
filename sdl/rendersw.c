/***************************************************************************
    SDL Software Video Rendering.  
    
    Known Bugs:
    - Scanlines don't work when Endian changed?

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#include "rendersw.h"
#include "frontend/config.h"
#include <stdint.h>
#include "../globals.h"
#include "../setup.h"
#include <SDL.h>

/* alekmaul's scaler taken from mame4all */
void bitmap_scale(uint32_t startx, uint32_t starty, uint32_t viswidth, uint32_t visheight, uint32_t newwidth, uint32_t newheight,uint32_t pitchsrc,uint32_t pitchdest, uint16_t* restrict src, uint16_t* restrict dst)
{
    uint32_t W,H,ix,iy,x,y;
    x=startx<<16;
    y=starty<<16;
    W=newwidth;
    H=newheight;
    ix=(viswidth<<16)/W;
    iy=(visheight<<16)/H;

    do 
    {
        uint16_t* restrict buffer_mem=&src[(y>>16)*pitchsrc];
        W=newwidth; x=startx<<16;
        do 
        {
            *dst++=buffer_mem[x>>16];
            x+=ix;
        } while (--W);
        dst+=pitchdest;
        y+=iy;
    } while (--H);
}


uint32_t my_min(uint32_t a, uint32_t b) { return a < b ? a : b; }

SDL_Surface *Render_surface, *real_screen;

// Palette Lookup
uint32_t Render_rgb[S16_PALETTE_ENTRIES * 3];    // Extended to hold shadow/hilight colours

uint16_t *Render_screen_pixels;

// Original Screen Width & Height
uint16_t Render_orig_width, Render_orig_height;

// --------------------------------------------------------------------------------------------
// Screen setup properties. Example below: 
// ________________________
// |  |                |  | <- screen size      (e.g. 1280 x 720)
// |  |                |  |
// |  |                |<-|--- destination size (e.g. 1027 x 720) to maintain aspect ratio
// |  |                |  | 
// |  |                |  |    source size      (e.g. 320  x 224) System 16 proportions
// |__|________________|__|
//
// --------------------------------------------------------------------------------------------

// Source texture / pixel array that we are going to manipulate
uint32_t Render_src_width, Render_src_height;

// Destination window width and height
uint32_t Render_dst_width, Render_dst_height;

// Screen width and height 
uint32_t Render_scn_width, Render_scn_height;

// Full-Screen, Stretch, Window
uint32_t Render_video_mode;
// Offsets (for full-screen mode, where x/y resolution isn't a multiple of the original height)
uint32_t Render_screen_xoff, Render_screen_yoff;

// SDL Pixel Format Codes. These differ between platforms.
uint8_t  Render_Rshift, Render_Gshift, Render_Bshift;
uint32_t Render_Rmask, Render_Gmask, Render_Bmask;

uint8_t Render_sdl_screen_size();

uint8_t Render_init(int src_width, int src_height, 
                    int scale,
                    int video_mode,
                    int scanlines)
{
    Render_src_width  = src_width;
    Render_src_height = src_height;
    Render_video_mode = video_mode;

    // Setup SDL Screen size
    if (!Render_sdl_screen_size())
        return 0;

    int32_t flags = SDL_FLAGS;
    
    SDL_ShowCursor(0);

    // If we're not stretching the screen, centre the image
    if (video_mode != VIDEO_MODE_STRETCH)
    {
        Render_screen_xoff = Render_scn_width - Render_dst_width;
        if (Render_screen_xoff)
            Render_screen_xoff = (Render_screen_xoff / 2);

        Render_screen_yoff = Render_scn_height - Render_dst_height;
        if (Render_screen_yoff) 
            Render_screen_yoff = (Render_screen_yoff / 2) * Render_scn_width;
    }
    // Otherwise set to the top-left corner
    else
    {
        Render_screen_xoff = 0;
        Render_screen_yoff = 0;
    }
    
    const uint8_t bpp = 16;
    // Frees (Deletes) existing surface
	if (Render_surface)
	{
		SDL_FreeSurface(Render_surface);
	}

	SDL_SetCursor(0);

	Render_scn_width = 320;
#ifdef CENTER_240
	Render_scn_height = 240;
#else
	Render_scn_height = 224;
#endif

#ifdef RS90_PORT
	real_screen = SDL_SetVideoMode(240, 160, 16, SDL_HWSURFACE
#ifdef SDL_TRIPLEBUF
	| SDL_TRIPLEBUF
#else
	| SDL_DOUBLEBUF
#endif
	);
	Render_surface = SDL_CreateRGBSurface(SDL_HWSURFACE, Render_scn_width, Render_scn_height, bpp, 0, 0, 0, 0);
#else
    // Set the video mode
	Render_surface = SDL_SetVideoMode(Render_scn_width, Render_scn_height, bpp, flags);
#endif

    if (!Render_surface)
    {
        fprintf(stderr, "Video mode set failed: %d.\n", SDL_GetError());
        return 0;
    }

    // Convert the SDL pixel surface to 16 bit.
    // This is potentially a larger surface area than the internal pixel array.
    Render_screen_pixels = (uint16_t*)Render_surface->pixels;
    
    // SDL Pixel Format Information
    Render_Rshift = Render_surface->format->Rshift;
    Render_Gshift = Render_surface->format->Gshift;
    Render_Bshift = Render_surface->format->Bshift;
    Render_Rmask  = Render_surface->format->Rmask;
    Render_Gmask  = Render_surface->format->Gmask;
    Render_Bmask  = Render_surface->format->Bmask;

    return 1;
}

void Render_disable()
{
	if (Render_surface)
		SDL_FreeSurface(Render_surface);
}

uint8_t Render_start_frame()
{
	return !(SDL_MUSTLOCK(Render_surface) && SDL_LockSurface(Render_surface) < 0);
}

uint8_t Render_finalize_frame()
{
	if (SDL_MUSTLOCK(Render_surface))
		SDL_UnlockSurface(Render_surface);
#ifdef RS90_PORT
	bitmap_scale(0, 0, 320, 224, 240, 160,320, 0, Render_surface->pixels, real_screen->pixels);
	SDL_Flip(real_screen);
#else
	SDL_Flip(Render_surface);
#endif
	
    return 1;
}

void Render_draw_frame(uint16_t* pixels)
{
	uint32_t i = 0;
	uint32_t x = 0, y = 0;
	uint32_t width = 0, height = 0;
	uint32_t skip = 0;
#ifdef CENTER_240
    uint16_t* spix = Render_screen_pixels + (1280 + 320);
#else
    uint16_t* spix = Render_screen_pixels;
#endif
    for (i = 0; i < (320 * 224); i++)
    {
		x++;
		if (x > 320)
		{
			x = 0;
			y++;
		}
		
		if (skip == 0)
		*(spix++) = Render_rgb[*(pixels++) & ((S16_PALETTE_ENTRIES * 3) - 1)]; 
	}
}

// Setup screen size
uint8_t Render_sdl_screen_size()
{
    if (Render_orig_width == 0 || Render_orig_height == 0)
    {
		Render_orig_width  = 320; 
#ifdef CENTER_240
		Render_orig_height = 240;
#else
		Render_orig_height = 224;
#endif
    }

    Render_scn_width  = Render_orig_width;
    Render_scn_height = Render_orig_height;

    return 1;
}

// See: SDL_PixelFormat
#define CURRENT_RGB() (r << Render_Rshift) | (g << Render_Gshift) | (b << Render_Bshift);

void Render_convert_palette(uint32_t adr, uint32_t r, uint32_t g, uint32_t b)
{
    adr >>= 1;

    r = (r) * (255 / 31);
    g = (g) * (255 / 31);
    b = (b) * (255 / 31);
    
	Render_rgb[adr] = (r << 8 | g << 3 | b >> 3);
      
    // Create shadow / highlight colours at end of RGB array
    // The resultant values are the same as MAME
    // 79105
    r = (r * 202) / 256;
    g = (g * 202) / 256;
    b = (b * 202) / 256;
    
    Render_rgb[adr + S16_PALETTE_ENTRIES] = Render_rgb[adr + (S16_PALETTE_ENTRIES * 2)] = ((r & 0b11111000) << 8) | ((g & 0b11111100) << 3) | (b >> 3);
}
