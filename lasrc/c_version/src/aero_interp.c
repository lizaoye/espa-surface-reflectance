#include "aero_interp.h"

/******************************************************************************
MODULE:  aerosol_interp

PURPOSE:  Interpolates the aerosol values throughout the image using the
aerosols that were calculated for each NxN window.

RETURN VALUE:
Type = N/A

PROJECT:  Land Satellites Data System Science Research and Development (LSRD)
at the USGS EROS

NOTES:
******************************************************************************/
void aerosol_interp
(
    Espa_internal_meta_t *xml_metadata, /* I: XML metadata information */
    uint16 *qaband,    /* I: QA band for the input image, nlines x nsamps */
    float *taero,      /* I/O: aerosol values for each pixel, nlines x nsamps
                          It is expected that the aerosol values are computed
                          for the center of the aerosol windows.  This routine
                          will fill in the pixels for the remaining, non-center
                          pixels of the window. */
    int nlines,        /* I: number of lines in qaband & taero bands */
    int nsamps         /* I: number of samps in qaband & taero bands */
)
{
    int i;                 /* looping variable for the bands */
    int line, samp;        /* looping variable for lines and samples */
    int curr_pix;          /* current pixel in 1D arrays of nlines * nsamps */
    int center_line;       /* line for the center of the aerosol window */
    int center_line1;      /* line+1 for the center of the aerosol window */
    int center_samp;       /* sample for the center of the aerosol window */
    int center_samp1;      /* sample+1 for the center of the aerosol window */
    int refl_indx = -99;   /* index of band 1 or first band */
    int aero_pix11;        /* pixel location for aerosol window values
                              [lcmg][scmg] */
    int aero_pix12;        /* pixel location for aerosol window values
                              [lcmg][scmg2] */
    int aero_pix21;        /* pixel location for aerosol window values
                              [lcmg2][scmg] */
    int aero_pix22;        /* pixel location for aerosol window values
                              [lcmg2][scmg2] */
    float xaero, yaero;    /* x/y location for aerosol pixel within the overall
                              larger aerosol window grid */
    float pixel_size_x;    /* x pixel size */
    float pixel_size_y;    /* y pixel size */
    float aero11;          /* aerosol value at window line, samp */
    float aero12;          /* aerosol value at window line, samp+1 */
    float aero21;          /* aerosol value at window line+1, samp */
    float aero22;          /* aerosol value at window line+1, samp+1 */
    float u, v;            /* line, sample fractional distance from current
                              pixel (weight applied to furthest line, sample) */
    float one_minus_u;     /* 1.0 - u (weight applied to closest line) */
    float one_minus_v;     /* 1.0 - v (weight applied to closest sample) */
    float one_minus_u_x_one_minus_v;  /* (1.0 - u) * (1.0 - v) */
    float one_minus_u_x_v; /* (1.0 - u) * v */
    float u_x_one_minus_v; /* u * (1.0 - v) */
    float u_x_v;           /* u * v */

#ifndef _OPENMP
    int tmp_percent = 0;  /* current percentage for printing status */
    int curr_tmp_percent; /* percentage for current line */
#endif

    /* Use band 1 band-related metadata for the reflectance information for
       Landsat (Level 1 products).  If band 1 isn't available then just use the
       first band in the XML file. */
    for (i = 0; i < xml_metadata->nbands; i++)
    {
        if (!strcmp (xml_metadata->band[i].name, "b1") &&
            !strncmp (xml_metadata->band[i].product, "L1", 2))
        {
            /* this is the index we'll use for Landsat band info */
            refl_indx = i;
        }
    }
    if (refl_indx == -99)
        refl_indx = 0;

    /* Copy the information from the XML file */
    pixel_size_x = xml_metadata->band[refl_indx].pixel_size[0];
    pixel_size_y = xml_metadata->band[refl_indx].pixel_size[1];

    /* Interpolate the aerosol data for each pixel location */
    printf ("Interpolating the aerosol data ...\n");
#ifdef _OPENMP
    tmp_percent = 0;
    #pragma omp parallel for private (line, samp, center_line, center_line1, center_samp, center_samp1, xaero, yaero, curr_pix, xaero, yaero, aero_pix11, aero_pix12, aero_pix21, aero_pix22, aero11, aero12, aero21, aero22, u, v, one_minus_u, one_minus_v, one_minus_u_x_one_minus_v, one_minus_u_x_v, u_x_one_minus_v, u_x_v)
#endif

    for (line = 0; line < nlines; line++)
    {
#ifndef _OPENMP
        /* update status, but not if multi-threaded */
        curr_tmp_percent = 100 * line / nlines;
        if (curr_tmp_percent > tmp_percent)
        {
            tmp_percent = curr_tmp_percent;
            if (tmp_percent % 10 == 0)
            {
                printf ("%d%% ", tmp_percent);
                fflush (stdout);
            }
        }
#endif

        /* Determine the fractional location of the center of this line in the
           aerosol windows */
        yaero = (line + 0.5) / AERO_WINDOW;
        u = yaero - (int) yaero;

        /* Determine the line of the representative center pixel in the
           aerosol NxN window array */
        center_line = (int) (line / AERO_WINDOW) * AERO_WINDOW +
            HALF_AERO_WINDOW;

        /* Determine if this pixel is closest to the line below or the line
           above. If the fractional value is in the top part of the aerosol
           window, then use the line above.  Otherwise use the line below. */
        if (u < 0.5)
        {
            center_line1 = center_line - AERO_WINDOW;

            /* If the aerosol window line value is outside the bounds of the
               scene, then just use the same line in the aerosol window */
            if (center_line1 < 0)
                center_line1 = center_line;
        }
        else
        {
            center_line1 = center_line + AERO_WINDOW;

            /* If the aerosol window line value is outside the bounds of the
               scene, then just use the same line in the aerosol window */
            if (center_line1 >= nlines-1)
                center_line1 = center_line;
        }

        curr_pix = line * nsamps;
        for (samp = 0; samp < nsamps; samp++, curr_pix++)
        {
            /* If this pixel is fill, then don't process */
            if (qaband[curr_pix] == 1)
                continue;

            /* Determine the fractional location of the center of this sample
               in the aerosol window */
            xaero = (samp + 0.5) / AERO_WINDOW;
            v = xaero - (int) xaero;

            /* Determine the sample of the representative center pixel in the
               aerosol NxN window array */
            center_samp = (int) (samp / AERO_WINDOW) * AERO_WINDOW +
                HALF_AERO_WINDOW;

            /* If the current line, sample are the same as the center line,
               sample, then skip to the next pixel.  We already have the
               aerosol value. */
            if (samp == center_samp && line == center_line)
                continue;

            /* Determine if this pixel is closest to the sample to the left or
               the sample to the right.  If the fractional value is on the left
               side of the aerosol window, then use the sample to the left.
               Otherwise use the sample to the right. */
            if (v < 0.5)
            {
                center_samp1 = center_samp - AERO_WINDOW;
    
                /* If the aerosol window sample value is outside the bounds of
                   the scene, then just use the same sample in the aerosol
                   window */
                if (center_samp1 < 0)
                    center_samp1 = center_samp;
            }
            else
            {
                center_samp1 = center_samp + AERO_WINDOW;
    
                /* If the aerosol window sample value is outside the bounds of
                   the scene, then just use the same sample in the aerosol
                   window */
                if (center_samp1 >= nsamps-1)
                    center_samp1 = center_samp;
            }
if (line == 95 && samp == 1719)
{
printf ("DEBUG: Line %d, samp %d ...\n", line, samp);
printf ("DEBUG:   Point 1: center line, center samp ... %d, %d\n", center_line, center_samp);
printf ("DEBUG:   Point 2: center line, center samp1 ... %d, %d\n", center_line, center_samp1);
printf ("DEBUG:   Point 3: center line1, center samp ... %d, %d\n", center_line1, center_samp);
printf ("DEBUG:   Point 4: center line1, center samp1 ... %d, %d\n", center_line1, center_samp1);
}

            /* Determine the four aerosol window pixels to be used for
               interpolating the current pixel */
            aero_pix11 = center_line * nsamps + center_samp;
            aero_pix12 = center_line * nsamps + center_samp1;
            aero_pix21 = center_line1 * nsamps + center_samp;
            aero_pix22 = center_line1 * nsamps + center_samp1;

            /* Get the aerosol values */
            aero11 = taero[aero_pix11];
            aero12 = taero[aero_pix12];
            aero21 = taero[aero_pix21];
            aero22 = taero[aero_pix22];
if (line == 95 && samp == 1719)
{
printf ("DEBUG:   Aerosol point 1 ... %f\n", aero11);
printf ("DEBUG:   Aerosol point 2 ... %f\n", aero12);
printf ("DEBUG:   Aerosol point 3 ... %f\n", aero21);
printf ("DEBUG:   Aerosol point 4 ... %f\n", aero22);
}

            /* Determine the fractional distance between the integer location
               and floating point pixel location to be used for interpolation */
            one_minus_u = 1.0 - u;
            one_minus_v = 1.0 - v;
            one_minus_u_x_one_minus_v = one_minus_u * one_minus_v;
            one_minus_u_x_v = one_minus_u * v;
            u_x_one_minus_v = u * one_minus_v;
            u_x_v = u * v;

            /* Interpolate the aerosol */
            taero[curr_pix] = aero11 * one_minus_u_x_one_minus_v +
                              aero12 * one_minus_u_x_v +
                              aero21 * u_x_one_minus_v +
                              aero22 * u_x_v;
if (line == 95 && samp == 1719)
printf ("DEBUG:   Current aerosol ... %f\n", taero[curr_pix]);
        }  /* end for samp */
    }  /* end for line */

#ifndef _OPENMP
    /* update status */
    printf ("100%%\n");
    fflush (stdout);
#endif
}


/******************************************************************************
MODULE:  aerosol_window_interp

PURPOSE:  Interpolates the aerosol window values which did not have successful
inversion.  The teps and ipflag values for those pixels will be updated,
based on this interpolation.

RETURN VALUE:
Type = int
Value           Description
-----           -----------
ERROR           Error interpolating the aerosols
SUCCESS         No errors encountered

PROJECT:  Land Satellites Data System Science Research and Development (LSRD)
at the USGS EROS

NOTES:
******************************************************************************/
int aerosol_window_interp
(
    uint16 *qaband,    /* I: QA band for the input image, nlines x nsamps */
    uint8 *ipflag,     /* I/O: QA flag to assist with aerosol interpolation,
                               nlines x nsamps.  It is expected that the ipflag
                               values are computed for the center of the
                               aerosol windows. */
    float *taero,      /* I/O: aerosol values for each pixel, nlines x nsamps
                          It is expected that the aerosol values are computed
                          for the center of the aerosol windows.  This routine
                          will interpolate/average the pixels of the windows
                          that failed the aerosol inversion (using ipflag) */
    float *teps,       /* I/O: eps (angstrom coefficient) for each pixel,
                               nlines x nsamps.  It is expected that the teps
                               values are computed for the center of the
                               aerosol windows. */
    int nlines,        /* I: number of lines in qaband & taero bands */
    int nsamps         /* I: number of samps in qaband & taero bands */
)
{
    char errmsg[STR_SIZE];                         /* error message */
    char FUNC_NAME[] = "aerosol_window_interp";    /* function name */
    int line, samp;       /* looping variable for lines and samples */
    int k, l;             /* looping variable for line, sample windows */
    int curr_pix;         /* current pixel in 1D arrays of nlines * nsamps */
    int win_pix;          /* current pixel in the line, sample window */
    int nbpixnf;          /* number of non-filled aerosol pixels */
    int prev_nbpixnf;     /* number of non-filled aerosol pixels in previous
                             loop */
    int nbpixtot;         /* total number of pixels in the window */
    float taeroavg;       /* average of the taero values in the window */
    float tepsavg;        /* average of the teps values in the window */
    int nbaeroavg;        /* number of pixels in the window used for computing
                             the taeroavg and tepsavg */
    bool *smflag = NULL;  /* flag for whether or not the window average was
                             computed and is valid for this pixel */
    float *taeros = NULL; /* array of the average taero values in the local
                             window, nlines x nsamps */
    float *tepss = NULL;  /* array of the average teps values in the local
                             window, nlines x nsamps */

    /* Allocate memory for smflag, taeros, teps, and tepss */
    smflag = calloc (nlines*nsamps, sizeof (bool));
    if (smflag == NULL)
    {
        sprintf (errmsg, "Error allocating memory for smflag");
        error_handler (true, FUNC_NAME, errmsg);
        return (ERROR);
    }

    taeros = calloc (nlines*nsamps, sizeof (float));
    if (taeros == NULL)
    {
        sprintf (errmsg, "Error allocating memory for taeros");
        error_handler (true, FUNC_NAME, errmsg);
        return (ERROR);
    }

    tepss = calloc (nlines*nsamps, sizeof (float));
    if (tepss == NULL)
    {
        sprintf (errmsg, "Error allocating memory for tepss");
        error_handler (true, FUNC_NAME, errmsg);
        return (ERROR);
    }

/* GAIL TODO Might change this 3x3 to 5x5 if the actual aerosol windows are small (i.e. 3x3 themselves). Leave as-is if the aerosol windows are 9x9 or 11x11. */
    /* Compute the average of the 3x3 window of aerosols (using the NxN windows)
       for each center window pixel that failed aerosol inversion */
    nbpixnf = 0;
    nbpixtot = 0;
    for (line = HALF_AERO_WINDOW; line < nlines; line += AERO_WINDOW)
    {
        curr_pix = line * nsamps + HALF_AERO_WINDOW;
        for (samp = HALF_AERO_WINDOW; samp < nsamps;
             samp += AERO_WINDOW, curr_pix += AERO_WINDOW)
        {
            /* Process non-fill pixels */
            smflag[curr_pix] = false;
            if (!level1_qa_is_fill (qaband[curr_pix]))
            {
                /* Initialize the variables */
                nbpixtot++;
                nbaeroavg = 0;
                taeroavg = 0.0;
                tepsavg = 0.0;

                /* Check the 3x3 window around the current pixel */
                for (k = line - AERO_WINDOW; k <= line + AERO_WINDOW;
                     k += AERO_WINDOW)
                {
                    /* Make sure the line is valid */
                    if (k < 0 || k >= nlines)
                        continue;

                    win_pix = k * nsamps + samp - AERO_WINDOW;
                    for (l = samp - AERO_WINDOW; l <= samp + AERO_WINDOW;
                         l += AERO_WINDOW, win_pix += AERO_WINDOW)
                    {
                        /* Make sure the sample is valid */
                        if (l < 0 || l >= nsamps)
                            continue;

                        /* If the pixel has clear retrieval of aerosols or
                           valid water aerosols then add it to the total sum */
                        if (btest (ipflag[win_pix], IPFLAG_CLEAR) ||
                            btest (ipflag[win_pix], IPFLAG_WATER))
                        {
                            nbaeroavg++;
                            taeroavg += taero[win_pix];
                            tepsavg += teps[win_pix];
                        }
                    }  /* for l */
                }  /* for k */

                /* If the number of clear/aerosol pixels in the window is high
                   enough (25%), then compute the average for the window */
                if (nbaeroavg > 2)
                {
                    taeroavg = taeroavg / nbaeroavg;
                    tepsavg = tepsavg / nbaeroavg;
                    taeros[curr_pix] = taeroavg;
                    tepss[curr_pix] = tepsavg;
                    smflag[curr_pix] = true;
                }
                else
                {
                    /* Keep track of the number of pixels where the count
                       was not high enough */
                    nbpixnf++;
                }
            }  /* if not fill */
        }  /* for samp */
    }  /* for line */

    /* Handle the case where none of the pixels were able to be averaged by
       setting default values.  Otherwise take a second pass through the
       pixels. */
    printf ("Second pass for aerosol interpolation ...\n");
    if (nbpixnf == nbpixtot)
    {
        /* Set defaults */
        for (line = HALF_AERO_WINDOW; line < nlines; line += AERO_WINDOW)
        {
            curr_pix = line * nsamps + HALF_AERO_WINDOW;
            for (samp = HALF_AERO_WINDOW; samp < nsamps;
                 samp += AERO_WINDOW, curr_pix += AERO_WINDOW)
            {
                /* If this is not a fill pixel then set default values */
                if (!level1_qa_is_fill (qaband[curr_pix]))
                {
                    taeros[curr_pix] = 0.05;
                    tepss[curr_pix] = 1.5;
                    smflag[curr_pix] = true;
                }
            }
        }
    }
    else
    {
        /* Second pass */
        prev_nbpixnf = 0;
        while (nbpixnf != 0)
        {
            /* If nothing was gained in the last loop then just use default
               values for the remaining pixels */
            if (nbpixnf == prev_nbpixnf)
            {
                for (line = HALF_AERO_WINDOW; line < nlines;
                     line += AERO_WINDOW)
                {
                    curr_pix = line * nsamps + HALF_AERO_WINDOW;
                    for (samp = HALF_AERO_WINDOW; samp < nsamps;
                         samp += AERO_WINDOW, curr_pix += AERO_WINDOW)
                    {
                        /* If this is not a fill pixel and the aerosol average
                           was not computed for this pixel */
                        if (!level1_qa_is_fill (qaband[curr_pix]) &&
                            !smflag[curr_pix])
                        {
                            taeros[curr_pix] = 0.05;
                            tepss[curr_pix] = 1.5;
                            smflag[curr_pix] = true;
                        }  /* if qaband and smflag */
                    }  /* for samp */
                }  /* for line */

                /* Break out of the while loop */
                break;
            }  /* if nbpixnf == prev_nbpixnf */

            prev_nbpixnf = nbpixnf;
            nbpixnf = 0;
            for (line = HALF_AERO_WINDOW; line < nlines; line += AERO_WINDOW)
            {
                curr_pix = line * nsamps + HALF_AERO_WINDOW;
                for (samp = HALF_AERO_WINDOW; samp < nsamps;
                     samp += AERO_WINDOW, curr_pix += AERO_WINDOW)
                {
                    /* If this is not a fill pixel and the aerosol average was
                       not computed for this pixel */
                    if (!level1_qa_is_fill (qaband[curr_pix]) &&
                        !smflag[curr_pix])
                    {
                        nbaeroavg = 0;
                        tepsavg = 0.0;
                        taeroavg = 0.0;

                        /* Check the 3x3 window around the current pixel */
                        for (k = line - AERO_WINDOW; k <= line + AERO_WINDOW;
                             k += AERO_WINDOW)
                        {
                            /* Make sure the line is valid */
                            if (k < 0 || k >= nlines)
                                continue;

                            win_pix = k * nsamps + samp - AERO_WINDOW;
                            for (l = samp - AERO_WINDOW;
                                 l <= samp + AERO_WINDOW;
                                 l += AERO_WINDOW, win_pix += AERO_WINDOW)
                            {
                                /* Make sure the sample is valid */
                                if (l < 0 || l >= nsamps)
                                    continue;

                                /* If the pixel has valid averages, then use
                                   them in this second pass */
                                if (smflag[win_pix])
                                {
                                    nbaeroavg++;
                                    taeroavg += taeros[win_pix];
                                    tepsavg += tepss[win_pix];
                                }
                            }  /* for l */
                        }  /* for k */

                        /* If at least one pixel in the window had valid
                           averages, then compute the average for the window
                           using the previously-computed window averages */
                        if (nbaeroavg > 0)
                        {
                            taeroavg = taeroavg / nbaeroavg;
                            tepsavg = tepsavg / nbaeroavg;
                            taeros[curr_pix] = taeroavg;
                            tepss[curr_pix] = tepsavg;
                            smflag[curr_pix] = true;
                        }
                        else
                        {
                            /* Keep track of the number of pixels where the
                               averages were not computed */
                            nbpixnf++;
                        }
                    }  /* if qaband and smflag */
                }  /* for samp */
            }  /* for line */
        }  /* while nbpixnf != 0 */
    }  /* else */

    /* Fill in the pixels where the aerosol retrieval failed */
    for (line = HALF_AERO_WINDOW; line < nlines; line += AERO_WINDOW)
    {
        curr_pix = line * nsamps + HALF_AERO_WINDOW;
        for (samp = HALF_AERO_WINDOW; samp < nsamps;
             samp += AERO_WINDOW, curr_pix += AERO_WINDOW)
        {
            if (btest (ipflag[curr_pix], IPFLAG_RETRIEVAL_FAIL))
            {
                taero[curr_pix] = taeros[curr_pix];
                teps[curr_pix] = tepss[curr_pix];

                /* Set the aerosol interpolated, but unset the retrieval
                   failed */
                ipflag[curr_pix] |= (1 << IPFLAG_INTERP);
                ipflag[curr_pix] &= ~(1 << IPFLAG_RETRIEVAL_FAIL);
            }
        }
    }

    /* Free memory */
    free (smflag);
    free (taeros);
    free (tepss);

    /* Successful completion */
    return (SUCCESS);
}
