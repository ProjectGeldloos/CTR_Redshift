/*
  CTR_Redshift - Redshift ported to Nintendo 3DS
  Copyright (C) 2017-2022 Sono (https://github.com/SonoSooS)
   
   This program is free software: you can redistribute it and/or modify  
   it under the terms of the GNU General Public License as   
   published by the Free Software Foundation, either version 3, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful, but 
   WITHOUT ANY WARRANTY; without even the implied warranty of 
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
   General Lesser Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

/*
  Redshift - https://github.com/jonls/redshift
  
   Redshift is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Redshift is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Redshift.  If not, see <http://www.gnu.org/licenses/>.

   Copyright (c) 2009-2017  Jon Lund Steffensen <jonlst@gmail.com>
*/

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


//Part of Redshift
#include "redshift.h"
#include "colorramp.h"

// Read color LUT at a given index and from a given screen
static void ReadAt(u32* dst, u8 idx, int screen)
{
    u32 pos = idx;
    GSPGPU_WriteHWRegs(screen & 1 ? 0x400580 : 0x400480, &pos, 4);
    GSPGPU_ReadHWRegs(screen & 1 ? 0x400584 : 0x400484, dst, 4);
}

// Write color LUT to a given index to a given screen
static void WriteAt(const u32* dst, u8 idx, int screen)
{
    u32 pos = idx & 0xFF;
    GSPGPU_WriteHWRegs(screen & 1 ? 0x400580 : 0x400480, &pos, 4);
    GSPGPU_WriteHWRegs(screen & 1 ? 0x400584 : 0x400484, dst, 4);
}

// Read the entire LUT from a given screen
static void ReadAll(u32* dst, int screen)
{
    u32 idx = 0;
    do
    {
        ReadAt(dst++, idx, screen);
    }
    while(++idx < 256);
}

// Write the entire LUT table back to a given screen
static void WriteAll(const u32* dst, int screen)
{
    u32 idx = 0;
    do
    {
        WriteAt(dst++, idx, screen);
    }
    while(++idx < 256);
}

// Clamp settings to their intended bounds
void ClampCS(color_setting_t* cs)
{
    if(cs->temperature < MIN_TEMP) cs->temperature = MIN_TEMP;
    if(cs->temperature > MAX_TEMP) cs->temperature = MAX_TEMP;
    
    if(cs->gamma[0] < MIN_GAMMA) cs->gamma[0] = MIN_GAMMA;
    if(cs->gamma[1] < MIN_GAMMA) cs->gamma[1] = MIN_GAMMA;
    if(cs->gamma[2] < MIN_GAMMA) cs->gamma[2] = MIN_GAMMA;
    if(cs->gamma[0] > MAX_GAMMA) cs->gamma[0] = MAX_GAMMA;
    if(cs->gamma[1] > MAX_GAMMA) cs->gamma[1] = MAX_GAMMA;
    if(cs->gamma[2] > MAX_GAMMA) cs->gamma[2] = MAX_GAMMA;
    
    if(cs->brightness < MIN_BRIGHTNESS) cs->brightness = MIN_BRIGHTNESS;
    if(cs->brightness > MAX_BRIGHTNESS) cs->brightness = MAX_BRIGHTNESS;
}

// Apply color settings to a given screen
void ApplyCS(color_setting_t* cs, int screen)
{
    u16* c = malloc(0x100 * sizeof(u16) * 3); // 256 entries of 3 colors of 16bit color-ramp values
    u8 i = 0;
    
    struct
    {
        u8 r;
        u8 g;
        u8 b;
        u8 z; // Unused
    } *px = malloc(0x100 * 4); // 256 entries of color LUT table (0x00BBGGRR)
    
    // Reading back the current curve is disabled, as it causes glitches applying on top of an existing curve
    //ReadAll((u32*)px, screen);
    
    // Fill in the default "identity" color curve used by screeninit
    do
    {
        *(u32*)&px[i] = i | (i << 8) | (i << 16);
    }
    while(++i); // Iterates 256 times because i is 8bit)
    
    // Convert the 3DS color curve LUT type into the type used by Redshift.
    // This is what values the color curve will apply *on top of*.
    do
    {
        *(c + i + 0x000) = px[i].r | (px[i].r << 8);
        *(c + i + 0x100) = px[i].g | (px[i].g << 8);
        *(c + i + 0x200) = px[i].b | (px[i].b << 8);
    }
    while(++i);
    
    // The redshift magic happens here
    colorramp_fill(c + 0x000, c + 0x100, c + 0x200, 0x100, cs);
    
    // Convert back from Redshift values back to 3DS color curve LUT type via truncation
    do
    {
        px[i].r = *(c + i + 0x000) >> 8;
        px[i].g = *(c + i + 0x100) >> 8;
        px[i].b = *(c + i + 0x200) >> 8;
    }
    while(++i);
    
    // Write the color curve to the screen
    WriteAll((u32*)px, screen);
    
    // Release temporary buffers
    free(px);
    free(c);
}


int main()
{
    gfxInit(GSP_RGBA8_OES, GSP_RGBA8_OES, false);
    
    gfxSetDoubleBuffering(GFX_TOP, 0);
    
    PrintConsole console;
    consoleInit(GFX_BOTTOM, &console);
    
    gfxSwapBuffers();
    gfxSwapBuffers();
    
    int redraw = 1;
    u32 kDown = 0;
    u32 kHeld = 0;
    u32 kUp = 0;
    
    touchPosition touch;
    
    int i = 0;
    int sel = 0;
    
    color_setting_t cs;
    memset(&cs, 0, sizeof(cs)); // Set default settings values
    cs.temperature = NEUTRAL_TEMP;
    cs.gamma[0] = 1.0F;
    cs.gamma[1] = 1.0F;
    cs.gamma[2] = 1.0F;
    cs.brightness = 1.0F;
    
    do
    {
        u32* ptr = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, 0, 0);
        memset(ptr, 0xFF, sizeof(*ptr) * 240 * 400); // Fill top screen with white
    }
    while(0);
    
    while(aptMainLoop())
    {
        hidScanInput();
        kDown = hidKeysDown();
        kHeld = hidKeysHeld();
        kUp = hidKeysUp();
        
        if(kHeld & KEY_SELECT)
            break;
        
        if(kHeld & KEY_TOUCH)
            hidTouchRead(&touch);
        
        if((kDown | kUp) || (kHeld & KEY_TOUCH)) // If a button statechange happens, redraw, as it might have affected the settings
            redraw = 1;
        
        if(kDown & KEY_RIGHT)
        {
            if(++sel > 4) sel = 4;
        }
        if(kDown & KEY_LEFT)
        {
            if(--sel < 0) sel = 0;
        }
        
        if(kDown & (KEY_X | KEY_Y)) // Pressing X or Y will reset the settings to default
        {
            memset(&cs, 0, sizeof(cs));
            cs.temperature = NEUTRAL_TEMP;
            cs.gamma[0] = 1.0F;
            cs.gamma[1] = 1.0F;
            cs.gamma[2] = 1.0F;
            cs.brightness = 1.0F;
        }
        
        if((((kDown & KEY_UP) ? 1 : 0) ^ ((kDown & KEY_DOWN) ? 1 : 0))) // If up and down aren't pressed at the same time
        {
            if(!sel) // Cursor on color temperature
            {
                if(kDown & KEY_UP)
                    cs.temperature += (kHeld & (KEY_L | KEY_R)) ? 1 : 100;
                else
                    cs.temperature -= (kHeld & (KEY_L | KEY_R)) ? 1 : 100;
            }
            else
            {
                float* f = 0;
                if(sel < 4) // Cursor is on one of the gamma values
                    f = &cs.gamma[sel - 1]; // -1 because sel here is 1-based relative to the gamma values
                
                if(sel == 4) // Cursor is on brightness
                    f = &cs.brightness;
                
                if(f)
                {
                    if(kDown & KEY_UP)
                        *f += (kHeld & (KEY_L | KEY_R)) ? 0.01F : 0.1F;
                    else
                        *f -= (kHeld & (KEY_L | KEY_R)) ? 0.01F : 0.1F;
                }
            }
        }
        
        if(kDown) // A button press must have affected settings values
        {
            // Clamp settings
            ClampCS(&cs);
            
            if(kHeld & KEY_A)
                ApplyCS(&cs, 1);
            if(kHeld & KEY_B)
                ApplyCS(&cs, 0);
        }
        
        if(redraw)
        {
            console.cursorX = 0;
            console.cursorY = 0;
            
            puts("CTR_Redshift v0.0 by Sono\n");
            
            if(kHeld & KEY_TOUCH)
            {
                printf("Touch: %03i x %03i\n", touch.px, touch.py);
            }
            else puts("\e[2K"); // Clear line
            
            
            printf("\n%c Colortemp: %iK\n\n", (sel == 0 ? '>' : ' '), cs.temperature);
            
            printf("%c Gamma[R]: %.2f\n", (sel == 1 ? '>' : ' '), cs.gamma[0]);
            printf("%c Gamma[G]: %.2f\n", (sel == 2 ? '>' : ' '), cs.gamma[1]);
            printf("%c Gamma[B]: %.2f\n", (sel == 3 ? '>' : ' '), cs.gamma[2]);
            
            printf("\n%c Brightness: %.2f\n\n", (sel == 4 ? '>' : ' '), cs.brightness);
            
            redraw = 0;
        }
        
        gspWaitForVBlank();
    }
    
    ded:
    
    gfxExit();
    
    return 0;
}
