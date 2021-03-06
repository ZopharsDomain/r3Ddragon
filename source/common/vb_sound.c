#include <stdbool.h>
#include <stdio.h>

#include "allegro_compat.h"
#include "vb_types.h"
#include "vb_set.h"
#include "v810_cpu.h"
#include "v810_mem.h"
#include "vb_sound.h"
#include "vb_lfsr.h"

#define CH1       0
#define CH2       1
#define CH3       2
#define CH4       3
#define CH5       4
#define CH6_0     5
#define CH6_1     6
#define CH6_2     7
#define CH6_3     8
#define CH6_4     9
#define CH6_5    10
#define CH6_6    11
#define CH6_7    12
#define CH_TOTAL 13

SAMPLE* channel[CH_TOTAL];
int voice[CH_TOTAL];
int Curr_C6V, C6V_playing = 0;
int snd_ram_changed[6] = {0, 0, 0, 0, 0, 0};

BYTE* Noise_Opt[8] = {Noise_Opt0, Noise_Opt1, Noise_Opt2, Noise_Opt3, Noise_Opt4, Noise_Opt5, Noise_Opt6, Noise_Opt7};
int Noise_Opt_Size[8] = {OPT0LEN, OPT1LEN, OPT2LEN, OPT3LEN, OPT4LEN, OPT5LEN, OPT6LEN, OPT7LEN};

// Set up Allegro sound stuff
void sound_init() {
    int i, index;

    if (!tVBOpt.SOUND)
        return;

    if (-1 == install_sound(DIGI_AUTODETECT, MIDI_NONE, NULL)) {
        dprintf(0, "[SND]: Error installing sound\n");
        tVBOpt.SOUND = 0;
        return;
    }

    for (i = CH1; i <= CH5; ++i) {
        channel[i] = create_sample(8, 0, 0, 32);
    }

    for (i = CH6_0; i <= CH6_7; ++i) {
        index = i - CH6_0;
        channel[i] = create_sample(8, 0, 0, Noise_Opt_Size[index]);
        memcpy(channel[i]->data, Noise_Opt[index], Noise_Opt_Size[index]);
    }

    for (i = 0; i < CH_TOTAL; ++i) {
        voice[i] = allocate_voice(channel[i]);
        voice_set_playmode(voice[i], PLAYMODE_LOOP);
    }

    // Set default to 0
    Curr_C6V = voice[CH6_0];
}

// Close Allegro sound stuff
void sound_close() {
    int i;

    if (!tVBOpt.SOUND)
        return;

    for (i = 0; i < CH_TOTAL; ++i) {
        voice_stop(voice[i]);
        destroy_sample(channel[i]);
        deallocate_voice(voice[i]);
    }
    remove_sound();
}

// FRQ reg converted to sampling frq for allegro
// manual says up to 2040, but any higher than 2038 crashes RB
// frequency * 32 samples per cycle
#define VB_FRQ_REG_TO_SAMP_FREQ(v) (5000000/(2048-(((v)>2038)?2038:(v))))
//#define VB_FRQ_REG_TO_SAMP_FREQ(v) (5000000/(2048-((v)*32)))

// Noise FRQ reg converted to sampling frq for allegro
#define RAND_FRQ_REG_TO_SAMP_FREQ(v) (500000/(2048-(v)))

// Handles updating allegro sounds according VB sound regs
void sound_update(int reg) {
    BYTE reg1, reg2; // Temporary regs
    int i, temp1, temp2;
    char waveram[32];
    const unsigned int wavelut[5] = {0x01000000, 0x01000080, 0x01000100, 0x01000180, 0x01000200};

    if (!tVBOpt.SOUND)
        return;

    // Notify of change in sound ram
    // Should check to make sure all channels disabled first (required on VB hardware)
    switch (reg & 0xFFFFFF80) {
        case WAVEDATA1:
            snd_ram_changed[0] = 1;
            break;
        case WAVEDATA2:
            snd_ram_changed[1] = 1;
            break;
        case WAVEDATA3:
            snd_ram_changed[2] = 1;
            break;
        case WAVEDATA4:
            snd_ram_changed[3] = 1;
            break;
        case WAVEDATA5:
            snd_ram_changed[4] = 1;
            break;
        case MODDATA:
            snd_ram_changed[5] = 1;
            break;
        default:
            break;
    }

    switch (reg) {
        // Channel 1
        case S1INT:
            reg1 = mem_rbyte(reg);
            if (reg1 & 0x80) {
                // Can only change RAM when disabled, so re-copy on enable in case sound RAM contents are changed
                if (snd_ram_changed[mem_rbyte(S1RAM)]) {
                    for (i = 0; i < 32; i++)
                        // Make 8 bit samples out of 6 bit samples
                        waveram[i] = (char) (mem_rbyte(wavelut[mem_rbyte(S1RAM)] + (i << 2)) << 2) ^ ((char)0x80);
                    memcpy(channel[CH1]->data, waveram, 32);
                    snd_ram_changed[mem_rbyte(S1RAM)] = 0;
                    // Output according to interval data
                }
                if (reg1 & 0x20) {
                    temp1 = 3.84f * (float) ((reg1 & 0x1F) + 1);
                    // Rig to stay at same freq, but for limited time (does this actually work?)
                    voice_sweep_frequency(voice[CH1], temp1, voice_get_frequency(voice[CH1]));
                }
                voice_set_position(voice[CH1], 0);
                voice_start(voice[CH1]);
            } else {
                voice_stop(voice[CH1]);
            }
            break;
        case S1LRV:
        case S1EV0:
        case S1EV1:
            reg1 = mem_rbyte(S1LRV);
            reg2 = mem_rbyte(S1EV0);
            // There's probably a better way to do volume/pan
            temp1 = (reg1 & 0x0F) | (reg1 >> 4); //OR L/R values
            temp2 = 4;
            // Find highest bit
            for (i = 0; i < 4; i++) {
                if (temp1 & 0x8) {
                    temp2 = i;
                    break;
                }
                temp1 <<= 1;
            }
            if (temp2 < 4) // L/R non-zero
                voice_set_volume(voice[CH1], (1 << (3 - temp2)) * (reg2 >> 4)); //multiply by envelope
            else
                voice_set_volume(voice[CH1], 0);
            voice_set_pan(voice[CH1], (128 + (((reg1 & 0x0F) - (reg1 >> 4)) * (8 << temp2))));

            // Envelope on
            if (reg2 & 0x01) {
                // Need to check bit D1 for repeat cycle (not sure how to do this)
                reg1 = mem_rbyte(S1EV1);
                if (reg1 & 0x08) { //grow
                    temp1 = 15.36f * (float) (((reg1 & 0x07) + 1) * ((255 - voice_get_volume(voice[CH1])) >> 4));
                    voice_ramp_volume(voice[CH1], temp1, 255);
                } else { // Decay
                    temp1 = 15.36f * (float) (((reg1 & 0x07) + 1) * ((voice_get_volume(voice[CH1]) - 0) >> 4));
                    voice_ramp_volume(voice[CH1], temp1, 0);
                }
            }
            break;
        case S1FQL:
        case S1FQH:
            reg1 = mem_rbyte(S1FQL);
            reg2 = mem_rbyte(S1FQH);
            voice_set_frequency(voice[CH1], VB_FRQ_REG_TO_SAMP_FREQ(((reg2 << 8) | reg1) & 0x7FF));
            break;
        case S1RAM:
            for (i = 0; i < 32; i++)
                // Make 8 bit samples out of 6 bit samples
                waveram[i] = (char) (mem_rbyte(wavelut[mem_rbyte(reg)] + (i << 2)) << 2) ^ ((char)0x80);
            memcpy(channel[CH1]->data, waveram, 32);
            break;

            // Channel 2
        case S2INT:
            reg1 = mem_rbyte(reg);
            if (reg1 & 0x80) {
                // Can only change RAM when disabled, so re-copy on enable in case sound RAM contents are changed
                if (snd_ram_changed[mem_rbyte(S2RAM)]) {
                    for (i = 0; i < 32; i++)
                        // Make 8 bit samples out of 6 bit samples
                        waveram[i] = (char) (mem_rbyte(wavelut[mem_rbyte(S2RAM)] + (i << 2)) << 2) ^ ((char)0x80);
                    memcpy(channel[CH2]->data, waveram, 32);
                    snd_ram_changed[mem_rbyte(S2RAM)] = 0;
                }
                if (reg1 & 0x20) { // Output according to interval data
                    temp1 = 3.84f * (float) ((reg1 & 0x1F) + 1);
                    // Rig to stay at same freq, but for limited time (does this actually work?)
                    voice_sweep_frequency(voice[CH2], temp1, voice_get_frequency(voice[CH2]));
                }
                voice_set_position(voice[CH2], 0);
                voice_start(voice[CH2]);
            } else {
                voice_stop(voice[CH2]);
            }
            break;
        case S2LRV:
        case S2EV0:
        case S2EV1:
            reg1 = mem_rbyte(S2LRV);
            reg2 = mem_rbyte(S2EV0);
            // There's probably a better way to do volume/pan
            temp1 = (reg1 & 0x0F) | (reg1 >> 4); //OR L/R values
            temp2 = 4;
            for (i = 0; i < 4; i++) { //find highest bit
                if (temp1 & 0x8) {
                    temp2 = i;
                    break;
                }
                temp1 <<= 1;
            }
            if (temp2 < 4) // L/R non-zero
                voice_set_volume(voice[CH2], (1 << (3 - temp2)) * (reg2 >> 4)); //multiply by envelope
            else
                voice_set_volume(voice[CH2], 0);
            voice_set_pan(voice[CH2], (128 + (((reg1 & 0x0F) - (reg1 >> 4)) * (8 << temp2))));

            if (reg2 & 0x01) { // Envelope on
                // Need to check bit D1 for repeat cycle (not sure how to do this)
                reg1 = mem_rbyte(S2EV1);
                if (reg1 & 0x08) { //grow
                    temp1 = 15.36f * (float) (((reg1 & 0x07) + 1) * ((255 - voice_get_volume(voice[CH2])) >> 4));
                    voice_ramp_volume(voice[CH2], temp1, 255);
                } else { // Decay
                    temp1 = 15.36f * (float) (((reg1 & 0x07) + 1) * ((voice_get_volume(voice[CH2]) - 0) >> 4));
                    voice_ramp_volume(voice[CH2], temp1, 0);
                }
            }
            break;
        case S2FQL:
        case S2FQH:
            reg1 = mem_rbyte(S2FQL);
            reg2 = mem_rbyte(S2FQH);
            voice_set_frequency(voice[CH2], VB_FRQ_REG_TO_SAMP_FREQ(((reg2 << 8) | reg1) & 0x7FF));
            break;
        case S2RAM:
            for (i = 0; i < 32; i++)
                // Make 8 bit samples out of 6 bit samples
                waveram[i] = (char) (mem_rbyte(wavelut[mem_rbyte(reg)] + (i << 2)) << 2) ^ ((char)0x80);
            memcpy(channel[CH2]->data, waveram, 32);
            break;

            // Channel 3
        case S3INT:
            reg1 = mem_rbyte(reg);
            if (reg1 & 0x80) {
                // Can only change RAM when disabled, so re-copy on enable in case sound RAM contents are changed
                if (snd_ram_changed[mem_rbyte(S3RAM)]) {
                    for (i = 0; i < 32; i++)
                        // Make 8 bit samples out of 6 bit samples
                        waveram[i] = (char) (mem_rbyte(wavelut[mem_rbyte(S3RAM)] + (i << 2)) << 2) ^ ((char)0x80);
                    memcpy(channel[CH3]->data, waveram, 32);
                    snd_ram_changed[mem_rbyte(S3RAM)] = 0;
                }
                if (reg1 & 0x20) { // Output according to interval data
                    temp1 = 3.84f * (float) ((reg1 & 0x1F) + 1);
                    // Rig to stay at same freq, but for limited time (does this actually work?)
                    voice_sweep_frequency(voice[CH3], temp1, voice_get_frequency(voice[CH3]));
                }
                voice_set_position(voice[CH3], 0);
                voice_start(voice[CH3]);
            } else {
                voice_stop(voice[CH3]);
            }
            break;
        case S3LRV:
        case S3EV0:
        case S3EV1:
            reg1 = mem_rbyte(S3LRV);
            reg2 = mem_rbyte(S3EV0);
            // There's probably a better way to do volume/pan
            temp1 = (reg1 & 0x0F) | (reg1 >> 4); // OR L/R values
            temp2 = 4;
            for (i = 0; i < 4; i++) { // Find highest bit
                if (temp1 & 0x8) {
                    temp2 = i;
                    break;
                }
                temp1 <<= 1;
            }
            if (temp2 < 4) // L/R non-zero
                voice_set_volume(voice[CH3], (1 << (3 - temp2)) * (reg2 >> 4)); //multiply by envelope
            else
                voice_set_volume(voice[CH3], 0);
            voice_set_pan(voice[CH3], (128 + (((reg1 & 0x0F) - (reg1 >> 4)) * (8 << temp2))));

            if (reg2 & 0x01) { // Envelope on
                // Need to check bit D1 for repeat cycle (not sure how to do this)
                reg1 = mem_rbyte(S3EV1);
                if (reg1 & 0x08) { // Grow
                    temp1 = 15.36f * (float) (((reg1 & 0x07) + 1) * ((255 - voice_get_volume(voice[CH3])) >> 4));
                    voice_ramp_volume(voice[CH3], temp1, 255);
                } else { // Decay
                    temp1 = 15.36f * (float) (((reg1 & 0x07) + 1) * ((voice_get_volume(voice[CH3]) - 0) >> 4));
                    voice_ramp_volume(voice[CH3], temp1, 0);
                }
            }
            break;
        case S3FQL:
        case S3FQH:
            reg1 = mem_rbyte(S3FQL);
            reg2 = mem_rbyte(S3FQH);
            voice_set_frequency(voice[CH3], VB_FRQ_REG_TO_SAMP_FREQ(((reg2 << 8) | reg1) & 0x7FF));
            break;
        case S3RAM:
            for (i = 0; i < 32; i++)
                // Make 8 bit samples out of 6 bit samples
                waveram[i] = (char) (mem_rbyte(wavelut[mem_rbyte(reg)] + (i << 2)) << 2) ^ ((char)0x80);
            memcpy(channel[CH3]->data, waveram, 32);
            break;

            // Channel 4
        case S4INT:
            reg1 = mem_rbyte(reg);
            if (reg1 & 0x80) {
                // Can only change RAM when disabled, so re-copy on enable in case sound RAM contents are changed
                if (snd_ram_changed[mem_rbyte(S4RAM)]) {
                    for (i = 0; i < 32; i++)
                        // Make 8 bit samples out of 6 bit samples
                        waveram[i] = (char) (mem_rbyte(wavelut[mem_rbyte(S4RAM)] + (i << 2)) << 2) ^ ((char)0x80);
                    memcpy(channel[CH4]->data, waveram, 32);
                    snd_ram_changed[mem_rbyte(S4RAM)] = 0;
                }
                if (reg1 & 0x20) { // Output according to interval data
                    temp1 = 3.84f * (float) ((reg1 & 0x1F) + 1);
                    // Rig to stay at same freq, but for limited time (does this actually work?)
                    voice_sweep_frequency(voice[CH4], temp1, voice_get_frequency(voice[CH4]));
                }
                voice_set_position(voice[CH4], 0);
                voice_start(voice[CH4]);
            } else {
                voice_stop(voice[CH4]);
            }
            break;
        case S4LRV:
        case S4EV0:
        case S4EV1:
            reg1 = mem_rbyte(S4LRV);
            reg2 = mem_rbyte(S4EV0);
            // There's probably a better way to do volume/pan
            temp1 = (reg1 & 0x0F) | (reg1 >> 4); // OR L/R values
            temp2 = 4;
            for (i = 0; i < 4; i++) { //Find highest bit
                if (temp1 & 0x8) {
                    temp2 = i;
                    break;
                }
                temp1 <<= 1;
            }
            if (temp2 < 4) // L/R non-zero
                voice_set_volume(voice[CH4], (1 << (3 - temp2)) * (reg2 >> 4)); //multiply by envelope
            else
                voice_set_volume(voice[CH4], 0);
            voice_set_pan(voice[CH4], (128 + (((reg1 & 0x0F) - (reg1 >> 4)) * (8 << temp2))));

            if (reg2 & 0x01) { // Envelope on
                // Need to check bit D1 for repeat cycle (not sure how to do this)
                reg1 = mem_rbyte(S4EV1);
                if (reg1 & 0x08) { // Grow
                    temp1 = 15.36f * (float) (((reg1 & 0x07) + 1) * ((255 - voice_get_volume(voice[CH4])) >> 4));
                    voice_ramp_volume(voice[CH4], temp1, 255);
                } else { // Decay
                    temp1 = 15.36f * (float) (((reg1 & 0x07) + 1) * ((voice_get_volume(voice[CH4]) - 0) >> 4));
                    voice_ramp_volume(voice[CH4], temp1, 0);
                }
            }
            break;
        case S4FQL:
        case S4FQH:
            reg1 = mem_rbyte(S4FQL);
            reg2 = mem_rbyte(S4FQH);
            voice_set_frequency(voice[CH4], VB_FRQ_REG_TO_SAMP_FREQ(((reg2 << 8) | reg1) & 0x7FF));
            break;
        case S4RAM:
            for (i = 0; i < 32; i++)
                // Make 8 bit samples out of 6 bit samples
                waveram[i] = (char) (mem_rbyte(wavelut[mem_rbyte(reg)] + (i << 2)) << 2) ^ ((char)0x80);
            memcpy(channel[CH4]->data, waveram, 32);
            break;

            // Channel 5
        case S5INT:
            reg1 = mem_rbyte(reg);
            if (reg1 & 0x80) {
                // Can only change RAM when disabled, so re-copy on enable in case sound RAM contents are changed
                if (snd_ram_changed[mem_rbyte(S5RAM)]) {
                    for (i = 0; i < 32; i++)
                        // Make 8 bit samples out of 6 bit samples
                        waveram[i] = (char) (mem_rbyte(wavelut[mem_rbyte(S5RAM)] + (i << 2)) << 2) ^ ((char)0x80);
                    memcpy(channel[CH5]->data, waveram, 32);
                    snd_ram_changed[mem_rbyte(S5RAM)] = 0;
                }
                if (reg1 & 0x20) { // Output according to interval data
                    temp1 = 3.84f * (float) ((reg1 & 0x1F) + 1);
                    // Rig to stay at same freq, but for limited time (does this actually work?)
                    voice_sweep_frequency(voice[CH5], temp1, voice_get_frequency(voice[CH5]));
                }
                voice_set_position(voice[CH5], 0);
                voice_start(voice[CH5]);
            } else {
                voice_stop(voice[CH5]);
            }
            break;
        case S5LRV:
        case S5EV0:
        case S5EV1:
        case S5SWP:
            reg1 = mem_rbyte(S5LRV);
            reg2 = mem_rbyte(S5EV0);
            // There's probably a better way to do volume/pan
            temp1 = (reg1 & 0x0F) | (reg1 >> 4); //OR L/R values
            temp2 = 4;
            for (i = 0; i < 4; i++) { // Find highest bit
                if (temp1 & 0x8) {
                    temp2 = i;
                    break;
                }
                temp1 <<= 1;
            }
            if (temp2 < 4) // L/R non-zero
                voice_set_volume(voice[CH5], (1 << (3 - temp2)) * (reg2 >> 4)); //multiply by envelope
            else
                voice_set_volume(voice[CH5], 0);
            voice_set_pan(voice[CH5], (128 + (((reg1 & 0x0F) - (reg1 >> 4)) * (8 << temp2))));

            if (reg2 & 0x01) { // Envelope on
                // Need to check bit D1 for repeat cycle (not sure how to do this)
                reg1 = mem_rbyte(S5EV1);
                if (reg1 & 0x08) { // Grow
                    temp1 = 15.36f * (float) (((reg1 & 0x07) + 1) * ((255 - voice_get_volume(voice[CH5])) >> 4));
                    voice_ramp_volume(voice[CH5], temp1, 255);
                } else { // Decay
                    temp1 = 15.36f * (float) (((reg1 & 0x07) + 1) * ((voice_get_volume(voice[CH5]) - 0) >> 4));
                    voice_ramp_volume(voice[CH5], temp1, 0);
                }
            }

            // Sweep/modulation stuff
            reg1 = mem_rbyte(S5EV1);
            if (reg1 & 0x40) { // Sweep/modulation enabled
                if (reg1 & 0x10) { // Modulation
                    //vb_printf("\nmodulation"); // Dunno how to do this yet
                } else { // Sweep
                    //vb_printf("\nsweep"); // Doubt this is right
                    reg1 = mem_rbyte(S5SWP);
                    //operation interval
                    if (reg1 & 0x80)
                        temp1 = 7.68f * (float) ((reg1 >> 4) & 0x07);
                    else
                        temp1 = 0.96f * (float) ((reg1 >> 4) & 0x07);
                    if (reg1 & 0x08) { // Add freq
                        // Amount of frequency needed to be sweeped * the time
                        // per increment / amount changed per increment+1 (not
                        // sure if +1 is right, but the others seem to do that,
                        // plus that avoids div by 0)
                        temp2 = ((0x7FF - (((mem_rbyte(S5FQH) << 8) | mem_rbyte(S5FQL)) & 0x7FF)) * temp1) / ((reg1 & 0x07) + 1);
                        voice_sweep_frequency(voice[CH5], temp2, VB_FRQ_REG_TO_SAMP_FREQ(0x7FF));
                    } else { // Subtract freq
                        // Amount of frequency needed to be sweeped * the time
                        // per increment / amount changed per increment+1 (not
                        // sure if +1 is right, but the others seem to do that,
                        // plus that avoids div by 0)
                        temp2 = (((((mem_rbyte(S5FQH) << 8) | mem_rbyte(S5FQL)) & 0x7FF) - 0) * temp1) / ((reg1 & 0x07) + 1);
                        voice_sweep_frequency(voice[CH5], temp2, VB_FRQ_REG_TO_SAMP_FREQ(0));
                    }
                }
            }
            break;
        case S5FQL:
        case S5FQH:
            reg1 = mem_rbyte(S5FQL);
            reg2 = mem_rbyte(S5FQH);
            voice_set_frequency(voice[CH5], VB_FRQ_REG_TO_SAMP_FREQ(((reg2 << 8) | reg1) & 0x7FF));
            break;
        case S5RAM:
            for (i = 0; i < 32; i++)
                // Make 8 bit samples out of 6 bit samples
                waveram[i] = (char) (mem_rbyte(wavelut[mem_rbyte(reg)] + (i << 2)) << 2) ^ ((char)0x80);
            memcpy(channel[CH5]->data, waveram, 32);
            break;

            // Channel 6
        case S6INT:
            reg1 = mem_rbyte(reg);
            if (reg1 & 0x80) {
                if (reg1 & 0x20) { // Output according to interval data
                    temp1 = 3.84f * ((reg1 & 0x1F) + 1);
                    // Rig to stay at same freq, but for limited time (does this actually work?)
                    voice_sweep_frequency(Curr_C6V, temp1, voice_get_frequency(Curr_C6V));
                }
                voice_set_position(Curr_C6V, 0);
                voice_start(Curr_C6V);
                C6V_playing = 1;
            } else {
                voice_stop(Curr_C6V);
                C6V_playing = 0;
            }
            break;
        case S6LRV:
        case S6EV0:
        case S6EV1:
            reg1 = mem_rbyte(S6LRV);
            reg2 = mem_rbyte(S6EV0);
            // There's probably a better way to do volume/pan
            temp1 = (reg1 & 0x0F) | (reg1 >> 4); //OR L/R values
            temp2 = 4;
            for (i = 0; i < 4; i++) { // Find highest bit
                if (temp1 & 0x8) {
                    temp2 = i;
                    break;
                }
                temp1 <<= 1;
            }
            if (temp2 < 4) // L/R non-zero
                voice_set_volume(Curr_C6V, (1 << (3 - temp2)) * (reg2 >> 4)); // Multiply by envelope
            else
                voice_set_volume(Curr_C6V, 0);
            voice_set_pan(Curr_C6V, (128 + (((reg1 & 0x0F) - (reg1 >> 4)) * (8 << temp2))));

            if (reg2 & 0x01) { // Envelope on
                // Need to check bit D1 for repeat cycle (not sure how to do this)
                reg1 = mem_rbyte(S4EV1);
                if (reg1 & 0x08) { // Grow
                    temp1 = 15.36f * ((reg1 & 0x07) + 1) * ((255 - voice_get_volume(Curr_C6V)) >> 4);
                    voice_ramp_volume(Curr_C6V, temp1, 255);
                } else { // Decay
                    temp1 = 15.36f * ((reg1 & 0x07) + 1) * ((voice_get_volume(Curr_C6V) - 0) >> 4);
                    voice_ramp_volume(Curr_C6V, temp1, 0);
                }
            }

            // Changing LFSR voice?  Rather than recopy LFSR sequence and
            // handle sequences of different lengths, just switch between
            // pre-allocated LFSR voices
            reg1 = mem_rbyte(S6EV1);
            temp1 = ((reg1 >> 4) & 0x07);
            temp1 += CH6_0;
            // temp1 is one of ch6_0 to ch_6_7
            if (Curr_C6V != voice[temp1]) {
                voice_stop(Curr_C6V);
                Curr_C6V = voice[temp1];
                if (C6V_playing == 1)
                    voice_start(Curr_C6V);
            }
            break;
        case S6FQL:
        case S6FQH:
            reg1 = mem_rbyte(S6FQL);
            reg2 = mem_rbyte(S6FQH);
            voice_set_frequency(Curr_C6V, RAND_FRQ_REG_TO_SAMP_FREQ(((reg2 << 8) | reg1) & 0x7FF));
            break;

        // Stop all sound output
        case SSTOP:
            for (i = 0; i < CH_TOTAL; ++i) {
                voice_stop(voice[i]);
            }
            break;

        default:
            break;
    }
}
