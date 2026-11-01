/*
 * Every external symbol of this library, renamed.
 *
 * Csound links libsndfile, and libsndfile carries its own GSM 06.10 -- the
 * same Degener/Bormann code, under the same names. Two `gsm_encode`s in one
 * static link is a duplicate-symbol error at best and, at worst, whichever
 * copy the linker happened to pick. So every function and table this copy
 * defines is spelled `vascular_` here, and this header is the first thing
 * gsm.h and private.h include.
 */
#ifndef VASCULAR_GSM_PREFIX_H
#define VASCULAR_GSM_PREFIX_H

#define gsm_create   vascular_gsm_create
#define gsm_destroy  vascular_gsm_destroy
#define gsm_encode   vascular_gsm_encode
#define gsm_decode   vascular_gsm_decode
#define gsm_option   vascular_gsm_option
#define gsm_print    vascular_gsm_print
#define gsm_explode  vascular_gsm_explode
#define gsm_implode  vascular_gsm_implode

#define Gsm_Coder                          vascular_Gsm_Coder
#define Gsm_Decoder                        vascular_Gsm_Decoder
#define Gsm_LPC_Analysis                   vascular_Gsm_LPC_Analysis
#define Gsm_Long_Term_Predictor            vascular_Gsm_Long_Term_Predictor
#define Gsm_Long_Term_Synthesis_Filtering  vascular_Gsm_Long_Term_Synthesis_Filtering
#define Gsm_Preprocess                     vascular_Gsm_Preprocess
#define Gsm_RPE_Decoding                   vascular_Gsm_RPE_Decoding
#define Gsm_RPE_Encoding                   vascular_Gsm_RPE_Encoding
#define Gsm_Short_Term_Analysis_Filter     vascular_Gsm_Short_Term_Analysis_Filter
#define Gsm_Short_Term_Synthesis_Filter    vascular_Gsm_Short_Term_Synthesis_Filter
#define Gsm_Encoding                       vascular_Gsm_Encoding
#define Gsm_Decoding                       vascular_Gsm_Decoding
#define Gsm_Update_of_reconstructed_short_time_residual_signal \
        vascular_Gsm_Update_of_reconstructed_short_time_residual_signal

#define gsm_A      vascular_gsm_A
#define gsm_B      vascular_gsm_B
#define gsm_DLB    vascular_gsm_DLB
#define gsm_FAC    vascular_gsm_FAC
#define gsm_H      vascular_gsm_H
#define gsm_INVA   vascular_gsm_INVA
#define gsm_MAC    vascular_gsm_MAC
#define gsm_MIC    vascular_gsm_MIC
#define gsm_NRFAC  vascular_gsm_NRFAC
#define gsm_QLB    vascular_gsm_QLB

#define gsm_L_add   vascular_gsm_L_add
#define gsm_L_asl   vascular_gsm_L_asl
#define gsm_L_asr   vascular_gsm_L_asr
#define gsm_L_mult  vascular_gsm_L_mult
#define gsm_L_sub   vascular_gsm_L_sub
#define gsm_abs     vascular_gsm_abs
#define gsm_add     vascular_gsm_add
#define gsm_asl     vascular_gsm_asl
#define gsm_asr     vascular_gsm_asr
#define gsm_div     vascular_gsm_div
#define gsm_mult    vascular_gsm_mult
#define gsm_mult_r  vascular_gsm_mult_r
#define gsm_norm    vascular_gsm_norm
#define gsm_sub     vascular_gsm_sub

#endif
