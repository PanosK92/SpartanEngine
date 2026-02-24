/*
Copyright(c) 2015-2026 Panos Karabelas & Thomas Ray

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES ========
#include "pch.h"
#include "TimeZone.h"
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
//===================

namespace spartan
{
    const TimeZoneDbEntry k_timezone_db[] =
    {
        {    -11.0f, "SST",     28.2167f,   -177.3667f, "UM", "Pacific/Midway", "US minor outlying islands - Midway Islands" },
        {    -11.0f, "NUT",    -19.0167f,   -169.9167f, "NU", "Pacific/Niue", "Niue - Niue" },
        {    -11.0f, "SST",    -14.2667f,      -170.7f, "AS", "Pacific/Pago_Pago", "Samoa (American) - Midway; Samoa (SST)" },
        {    -10.0f, "HST",       51.88f,   -176.6581f, "US", "America/Adak", "United States - Alaska - western Aleutians" },
        {    -10.0f, "HST",     21.3069f,   -157.8583f, "US", "Pacific/Honolulu", "United States - Hawaii" },
        {    -10.0f, "CKT",    -21.2333f,   -159.7667f, "CK", "Pacific/Rarotonga", "Cook Islands" },
        {    -10.0f, "TAHT",    -17.5333f,   -149.5667f, "PF", "Pacific/Tahiti", "French Polynesia - Society Islands" },
        {     -9.5f, "MART",        -9.0f,      -139.5f, "PF", "Pacific/Marquesas", "French Polynesia - Marquesas Islands" },
        {     -9.0f, "AKST",     61.2181f,   -149.9003f, "US", "America/Anchorage", "United States - Alaska (most areas)" },
        {     -9.0f, "AKST",     58.3019f,   -134.4197f, "US", "America/Juneau", "United States - Alaska - Juneau area" },
        {     -9.0f, "AKST",     55.1269f,   -131.5764f, "US", "America/Metlakatla", "United States - Alaska - Annette Island" },
        {     -9.0f, "AKST",     64.5011f,   -165.4064f, "US", "America/Nome", "United States - Alaska (west)" },
        {     -9.0f, "AKST",     57.1764f,   -135.3019f, "US", "America/Sitka", "United States - Alaska - Sitka area" },
        {     -9.0f, "AKST",     59.5469f,   -139.7272f, "US", "America/Yakutat", "United States - Alaska - Yakutat" },
        {     -9.0f, "GAMT",    -23.1333f,     -134.95f, "PF", "Pacific/Gambier", "French Polynesia - Gambier Islands" },
        {     -8.0f, "Tijuana",     34.0522f,   -118.2428f, "US", "America/Los_Angeles", "United States - Pacific" },
        {     -8.0f, "Tijuana",     32.5333f,   -117.0167f, "MX", "America/Tijuana", "Mexico - Baja California" },
        {     -8.0f, "Tijuana",     49.2667f,   -123.1167f, "CA", "America/Vancouver", "Canada - Pacific - BC (most areas)" },
        {     -8.0f, "PST",    -25.0667f,   -130.0833f, "PN", "Pacific/Pitcairn", "Pitcairn - Pitcairn" },
        {     -7.0f, "MST",     43.6136f,   -116.2025f, "US", "America/Boise", "United States - Mountain - ID (south), OR (east)" },
        {     -7.0f, "MST",     69.1139f,   -105.0528f, "CA", "America/Cambridge_Bay", "Canada - Mountain - NU (west)" },
        {     -7.0f, "MST",     28.6333f,   -106.0833f, "MX", "America/Chihuahua", "Mexico - Chihuahua (most areas)" },
        {     -7.0f, "MST",     31.7333f,   -106.4833f, "MX", "America/Ciudad_Juarez", "Mexico - Chihuahua (US border - west)" },
        {     -7.0f, "MST",        49.1f,   -116.5167f, "CA", "America/Creston", "Canada - MST - BC (Creston)" },
        {     -7.0f, "MST",     64.0667f,   -139.4167f, "CA", "America/Dawson", "Canada - MST - Yukon (west)" },
        {     -7.0f, "MST",     55.7667f,   -120.2333f, "CA", "America/Dawson_Creek", "Canada - MST - BC (Dawson Cr, Ft St John)" },
        {     -7.0f, "MST",     39.7392f,   -104.9842f, "US", "America/Denver", "United States - Mountain (most areas)" },
        {     -7.0f, "MST",       53.55f,   -113.4667f, "CA", "America/Edmonton", "Canada - Mountain - AB, BC(E), NT(E), SK(W)" },
        {     -7.0f, "MST",        58.8f,      -122.7f, "CA", "America/Fort_Nelson", "Canada - MST - BC (Ft Nelson)" },
        {     -7.0f, "MST",     29.0667f,   -110.9667f, "MX", "America/Hermosillo", "Mexico - Sonora" },
        {     -7.0f, "MST",     68.3497f,   -133.7167f, "CA", "America/Inuvik", "Canada - Mountain - NT (west)" },
        {     -7.0f, "MST",     23.2167f,   -106.4167f, "MX", "America/Mazatlan", "Mexico - Baja California Sur, Nayarit (most areas), Sinaloa" },
        {     -7.0f, "MST",     29.5667f,   -104.4167f, "MX", "America/Ojinaga", "Mexico - Chihuahua (US border - east)" },
        {     -7.0f, "MST",     33.4483f,   -112.0733f, "US", "America/Phoenix", "United States - MST - AZ (except Navajo)" },
        {     -7.0f, "MST",     60.7167f,     -135.05f, "CA", "America/Whitehorse", "Canada - MST - Yukon (east)" },
        {     -6.0f, "CST",        20.8f,     -105.25f, "MX", "America/Bahia_Banderas", "Mexico - Bahia de Banderas" },
        {     -6.0f, "CST",        17.5f,       -88.2f, "BZ", "America/Belize", "Belize" },
        {     -6.0f, "CST",       41.85f,      -87.65f, "US", "America/Chicago", "United States - Central (most areas)" },
        {     -6.0f, "CST",      9.9333f,    -84.0833f, "CR", "America/Costa_Rica", "Costa Rica" },
        {     -6.0f, "CST",        13.7f,       -89.2f, "SV", "America/El_Salvador", "El Salvador" },
        {     -6.0f, "CST",     14.6333f,    -90.5167f, "GT", "America/Guatemala", "Guatemala" },
        {     -6.0f, "CST",     41.2958f,     -86.625f, "US", "America/Indiana/Knox", "United States - Central - IN (Starke)" },
        {     -6.0f, "CST",     37.9531f,    -86.7614f, "US", "America/Indiana/Tell_City", "United States - Central - IN (Perry)" },
        {     -6.0f, "CST",       12.15f,    -86.2833f, "NI", "America/Managua", "Nicaragua" },
        {     -6.0f, "CST",     25.8333f,       -97.5f, "MX", "America/Matamoros", "Mexico - Coahuila, Nuevo Leon, Tamaulipas (US border)" },
        {     -6.0f, "CST",     45.1078f,    -87.6142f, "US", "America/Menominee", "United States - Central - MI (Wisconsin border)" },
        {     -6.0f, "CST",     20.9667f,    -89.6167f, "MX", "America/Merida", "Mexico - Campeche, Yucatan" },
        {     -6.0f, "CST",        19.4f,      -99.15f, "MX", "America/Mexico_City", "Mexico - Central Mexico" },
        {     -6.0f, "CST",     25.6667f,   -100.3167f, "MX", "America/Monterrey", "Mexico - Durango; Coahuila, Nuevo Leon, Tamaulipas (most areas)" },
        {     -6.0f, "CST",     47.2642f,   -101.7778f, "US", "America/North_Dakota/Beulah", "United States - Central - ND (Mercer)" },
        {     -6.0f, "CST",     47.1164f,   -101.2992f, "US", "America/North_Dakota/Center", "United States - Central - ND (Oliver)" },
        {     -6.0f, "CST",      46.845f,   -101.4108f, "US", "America/North_Dakota/New_Salem", "United States - Central - ND (Morton rural)" },
        {     -6.0f, "CST",     62.8167f,    -92.0831f, "CA", "America/Rankin_Inlet", "Canada - Central - NU (central)" },
        {     -6.0f, "CST",        50.4f,     -104.65f, "CA", "America/Regina", "Canada - CST - SK (most areas)" },
        {     -6.0f, "CST",     74.6956f,    -94.8292f, "CA", "America/Resolute", "Canada - Central - NU (Resolute)" },
        {     -6.0f, "CST",     50.2833f,   -107.8333f, "CA", "America/Swift_Current", "Canada - CST - SK (midwest)" },
        {     -6.0f, "CST",        14.1f,    -87.2167f, "HN", "America/Tegucigalpa", "Honduras" },
        {     -6.0f, "CST",     49.8833f,      -97.15f, "CA", "America/Winnipeg", "Canada - Central - ON (west), Manitoba" },
        {     -6.0f, "EAST",      -27.15f,   -109.4333f, "CL", "Pacific/Easter", "Chile - Easter Island" },
        {     -6.0f, "GALT",        -0.9f,       -89.6f, "EC", "Pacific/Galapagos", "Ecuador - Galapagos Islands" },
        {     -5.0f, "EST",     48.7586f,    -91.6217f, "CA", "America/Atikokan", "Canada - EST - ON (Atikokan), NU (Coral H)" },
        {     -5.0f, "COT",         4.6f,    -74.0833f, "CO", "America/Bogota", "Colombia" },
        {     -5.0f, "EST",     21.0833f,    -86.7667f, "MX", "America/Cancun", "Mexico - Quintana Roo" },
        {     -5.0f, "EST",        19.3f,    -81.3833f, "KY", "America/Cayman", "Cayman Islands" },
        {     -5.0f, "EST",     42.3314f,    -83.0458f, "US", "America/Detroit", "United States - Eastern - MI (most areas)" },
        {     -5.0f, "ACT",     -6.6667f,    -69.8667f, "BR", "America/Eirunepe", "Brazil - Amazonas (west)" },
        {     -5.0f, "ECT",     -2.1667f,    -79.8333f, "EC", "America/Guayaquil", "Ecuador - Ecuador (mainland)" },
        {     -5.0f, "CST",     23.1333f,    -82.3667f, "CU", "America/Havana", "Cuba - Cuba" },
        {     -5.0f, "EST",     39.7683f,    -86.1581f, "US", "America/Indiana/Indianapolis", "United States - Eastern - IN (most areas)" },
        {     -5.0f, "EST",     38.3756f,    -86.3447f, "US", "America/Indiana/Marengo", "United States - Eastern - IN (Crawford)" },
        {     -5.0f, "EST",     38.4919f,    -87.2786f, "US", "America/Indiana/Petersburg", "United States - Eastern - IN (Pike)" },
        {     -5.0f, "EST",     38.7478f,    -85.0672f, "US", "America/Indiana/Vevay", "United States - Eastern - IN (Switzerland)" },
        {     -5.0f, "EST",     38.6772f,    -87.5286f, "US", "America/Indiana/Vincennes", "United States - Eastern - IN (Da, Du, K, Mn)" },
        {     -5.0f, "EST",     41.0514f,    -86.6031f, "US", "America/Indiana/Winamac", "United States - Eastern - IN (Pulaski)" },
        {     -5.0f, "EST",     63.7333f,    -68.4667f, "CA", "America/Iqaluit", "Canada - Eastern - NU (most areas)" },
        {     -5.0f, "EST",     17.9681f,    -76.7933f, "JM", "America/Jamaica", "Jamaica - Eastern Standard (EST) - Caymans; Jamaica; eastern Mexico; Panama" },
        {     -5.0f, "EST",     38.2542f,    -85.7594f, "US", "America/Kentucky/Louisville", "United States - Eastern - KY (Louisville area)" },
        {     -5.0f, "EST",     36.8297f,    -84.8492f, "US", "America/Kentucky/Monticello", "United States - Eastern - KY (Wayne)" },
        {     -5.0f, "EST",      -12.05f,      -77.05f, "PE", "America/Lima", "Peru - eastern South America" },
        {     -5.0f, "EST",     25.0833f,      -77.35f, "BS", "America/Nassau", "Bahamas" },
        {     -5.0f, "EST",     40.7142f,    -74.0064f, "US", "America/New_York", "United States - Eastern (most areas)" },
        {     -5.0f, "EST",      8.9667f,    -79.5333f, "PA", "America/Panama", "Panama" },
        {     -5.0f, "EST",     18.5333f,    -72.3333f, "HT", "America/Port-au-Prince", "Haiti" },
        {     -5.0f, "EST",     -9.9667f,       -67.8f, "BR", "America/Rio_Branco", "Brazil - Acre" },
        {     -5.0f, "EST",       43.65f,    -79.3833f, "CA", "America/Toronto", "Canada - Eastern - ON & QC (most areas)" },
        {     -4.0f, "AST",       17.05f,       -61.8f, "AG", "America/Antigua", "Antigua & Barbuda" },
        {     -4.0f, "AST",        12.5f,    -69.9667f, "AW", "America/Aruba", "Aruba" },
        {     -4.0f, "AST",    -25.2667f,    -57.6667f, "PY", "America/Asuncion", "Paraguay" },
        {     -4.0f, "AST",        13.1f,    -59.6167f, "BB", "America/Barbados", "Barbados" },
        {     -4.0f, "AST",     51.4167f,    -57.1167f, "CA", "America/Blanc-Sablon", "Canada - AST - QC (Lower North Shore)" },
        {     -4.0f, "AST",      2.8167f,    -60.6667f, "BR", "America/Boa_Vista", "Brazil - Roraima" },
        {     -4.0f, "AST",      -20.45f,    -54.6167f, "BR", "America/Campo_Grande", "Brazil - Mato Grosso do Sul" },
        {     -4.0f, "VET",        10.5f,    -66.9333f, "VE", "America/Caracas", "Venezuela - western South America" },
        {     -4.0f, "CLST",    -45.5667f,    -72.0667f, "CL", "America/Coyhaique", "Chile - Aysen Region" },
        {     -4.0f, "AMT",    -15.5833f,    -56.0833f, "BR", "America/Cuiaba", "Brazil - Mato Grosso" },
        {     -4.0f, "AST",     12.1833f,       -69.0f, "CW", "America/Curacao", "Curaçao" },
        {     -4.0f, "AST",        15.3f,       -61.4f, "DM", "America/Dominica", "Dominica" },
        {     -4.0f, "AST",        46.2f,      -59.95f, "CA", "America/Glace_Bay", "Canada - Atlantic - NS (Cape Breton)" },
        {     -4.0f, "AST",     53.3333f,    -60.4167f, "CA", "America/Goose_Bay", "Canada - Atlantic - Labrador (most areas)" },
        {     -4.0f, "AST",     21.4667f,    -71.1333f, "TC", "America/Grand_Turk", "Turks & Caicos Is" },
        {     -4.0f, "AST",       12.05f,      -61.75f, "GD", "America/Grenada", "Grenada" },
        {     -4.0f, "AST",     16.2333f,    -61.5333f, "GP", "America/Guadeloupe", "Guadeloupe" },
        {     -4.0f, "AST",         6.8f,    -58.1667f, "GY", "America/Guyana", "Guyana" },
        {     -4.0f, "AST",       44.65f,       -63.6f, "CA", "America/Halifax", "Canada - Atlantic - NS (most areas), PE" },
        {     -4.0f, "AST",     12.1508f,    -68.2767f, "BQ", "America/Kralendijk", "Caribbean NL" },
        {     -4.0f, "AST",       -16.5f,      -68.15f, "BO", "America/La_Paz", "Bolivia" },
        {     -4.0f, "AST",     18.0514f,    -63.0472f, "SX", "America/Lower_Princes", "St Maarten (Dutch)" },
        {     -4.0f, "AST",     -3.1333f,    -60.0167f, "BR", "America/Manaus", "Brazil - Amazonas (east)" },
        {     -4.0f, "AST",     18.0667f,    -63.0833f, "MF", "America/Marigot", "St Martin (French)" },
        {     -4.0f, "AST",        14.6f,    -61.0833f, "MQ", "America/Martinique", "Martinique" },
        {     -4.0f, "AST",        46.1f,    -64.7833f, "CA", "America/Moncton", "Canada - Atlantic - New Brunswick" },
        {     -4.0f, "AST",     16.7167f,    -62.2167f, "MS", "America/Montserrat", "Montserrat" },
        {     -4.0f, "AST",       10.65f,    -61.5167f, "TT", "America/Port_of_Spain", "Trinidad & Tobago" },
        {     -4.0f, "AST",     -8.7667f,       -63.9f, "BR", "America/Porto_Velho", "Brazil - Rondonia" },
        {     -4.0f, "AST",     18.4683f,    -66.1061f, "PR", "America/Puerto_Rico", "Puerto Rico" },
        {     -4.0f, "AST",     -2.4333f,    -54.8667f, "BR", "America/Santarem", "Brazil - Para (west)" },
        {     -4.0f, "AST",      -33.45f,    -70.6667f, "CL", "America/Santiago", "Chile - most of Chile" },
        {     -4.0f, "AST",     18.4667f,       -69.9f, "DO", "America/Santo_Domingo", "Dominican Republic - Atlantic Standard (AST) - eastern Caribbean" },
        {     -4.0f, "AST",     17.8833f,      -62.85f, "BL", "America/St_Barthelemy", "St Barthelemy" },
        {     -4.0f, "AST",        17.3f,    -62.7167f, "KN", "America/St_Kitts", "St Kitts & Nevis" },
        {     -4.0f, "AST",     14.0167f,       -61.0f, "LC", "America/St_Lucia", "St Lucia" },
        {     -4.0f, "AST",       18.35f,    -64.9333f, "VI", "America/St_Thomas", "Virgin Islands (US)" },
        {     -4.0f, "AST",       13.15f,    -61.2333f, "VC", "America/St_Vincent", "St Vincent" },
        {     -4.0f, "AST",     76.5667f,    -68.7833f, "GL", "America/Thule", "Greenland - Thule/Pituffik" },
        {     -4.0f, "AST",       18.45f,    -64.6167f, "VG", "America/Tortola", "Virgin Islands (UK)" },
        {     -4.0f, "AST",     32.2833f,    -64.7667f, "BM", "Atlantic/Bermuda", "Bermuda" },
        {     -3.5f, "GMT",     47.5667f,    -52.7167f, "CA", "America/St_Johns", "Canada - Newfoundland, Labrador (SE)" },
        {     -3.0f, "BRT",        -7.2f,       -48.2f, "BR", "America/Araguaina", "Brazil - Tocantins" },
        {     -3.0f, "BRT",       -34.6f,      -58.45f, "AR", "America/Argentina/Buenos_Aires", "Argentina - Buenos Aires (BA, CF)" },
        {     -3.0f, "ART",    -28.4667f,    -65.7833f, "AR", "America/Argentina/Catamarca", "Argentina - Catamarca (CT), Chubut (CH)" },
        {     -3.0f, "ART",       -31.4f,    -64.1833f, "AR", "America/Argentina/Cordoba", "Argentina - Argentina (most areas: CB, CC, CN, ER, FM, MN, SE, SF)" },
        {     -3.0f, "ART",    -24.1833f,       -65.3f, "AR", "America/Argentina/Jujuy", "Argentina - Jujuy (JY)" },
        {     -3.0f, "ART",    -29.4333f,      -66.85f, "AR", "America/Argentina/La_Rioja", "Argentina - La Rioja (LR)" },
        {     -3.0f, "ART",    -32.8833f,    -68.8167f, "AR", "America/Argentina/Mendoza", "Argentina - Mendoza (MZ)" },
        {     -3.0f, "ART",    -51.6333f,    -69.2167f, "AR", "America/Argentina/Rio_Gallegos", "Argentina - Santa Cruz (SC)" },
        {     -3.0f, "ART",    -24.7833f,    -65.4167f, "AR", "America/Argentina/Salta", "Argentina - Salta (SA, LP, NQ, RN)" },
        {     -3.0f, "ART",    -31.5333f,    -68.5167f, "AR", "America/Argentina/San_Juan", "Argentina - San Juan (SJ)" },
        {     -3.0f, "ART",    -33.3167f,      -66.35f, "AR", "America/Argentina/San_Luis", "Argentina - San Luis (SL)" },
        {     -3.0f, "ART",    -26.8167f,    -65.2167f, "AR", "America/Argentina/Tucuman", "Argentina - Tucuman (TM)" },
        {     -3.0f, "ART",       -54.8f,       -68.3f, "AR", "America/Argentina/Ushuaia", "Argentina - Tierra del Fuego (TF)" },
        {     -3.0f, "BRT",    -12.9833f,    -38.5167f, "BR", "America/Bahia", "Brazil - Bahia" },
        {     -3.0f, "BRT",       -1.45f,    -48.4833f, "BR", "America/Belem", "Brazil - Para (east), Amapa" },
        {     -3.0f, "GFT",      4.9333f,    -52.3333f, "GF", "America/Cayenne", "French Guiana" },
        {     -3.0f, "BRT",     -3.7167f,       -38.5f, "BR", "America/Fortaleza", "Brazil - Brazil (northeast: MA, PI, CE, RN, PB)" },
        {     -3.0f, "BRT",     -9.6667f,    -35.7167f, "BR", "America/Maceio", "Brazil - Alagoas, Sergipe" },
        {     -3.0f, "PMST",       47.05f,    -56.3333f, "PM", "America/Miquelon", "St Pierre & Miquelon - St Pierre & Miquelon" },
        {     -3.0f, "UYT",    -34.9092f,    -56.2125f, "UY", "America/Montevideo", "Uruguay" },
        {     -3.0f, "SRT",      5.8333f,    -55.1667f, "SR", "America/Paramaribo", "Suriname" },
        {     -3.0f, "CLST",      -53.15f,    -70.9167f, "CL", "America/Punta_Arenas", "Chile - Magallanes Region" },
        {     -3.0f, "BRT",       -8.05f,       -34.9f, "BR", "America/Recife", "Brazil - Pernambuco" },
        {     -3.0f, "BRT",    -23.5333f,    -46.6167f, "BR", "America/Sao_Paulo", "Brazil - Brazil (southeast: GO, DF, MG, ES, RJ, SP, PR, SC, RS)" },
        {     13.0f, "NZDT",       -64.8f,       -64.1f, "AQ", "Antarctica/Palmer", "Antarctica - Palmer" },
        {     -3.0f, "ART",    -67.5667f,    -68.1333f, "AQ", "Antarctica/Rothera", "Antarctica - Rothera" },
        {     -3.0f, "FKST",       -51.7f,      -57.85f, "FK", "Atlantic/Stanley", "Falkland Islands" },
        {     -2.0f, "FNT",       -3.85f,    -32.4167f, "BR", "America/Noronha", "Brazil - Atlantic islands" },
        {     -2.0f, "WGST",     64.1833f,    -51.7333f, "GL", "America/Nuuk", "Greenland - most of Greenland" },
        {     -2.0f, "GST",    -54.2667f,    -36.5333f, "GS", "Atlantic/South_Georgia", "South Georgia & the South Sandwich Islands" },
        {     -1.0f, "WGST",     70.4833f,    -21.9667f, "GL", "America/Scoresbysund", "Greenland - Scoresbysund/Ittoqqortoormiit" },
        {     -1.0f, "AZOT",     37.7333f,    -25.6667f, "PT", "Atlantic/Azores", "Portugal - Azores" },
        {     -1.0f, "CVT",     14.9167f,    -23.5167f, "CV", "Atlantic/Cape_Verde", "Cape Verde - Cape Verde" },
        {      0.0f, "GMT",      5.3167f,     -4.0333f, "CI", "Africa/Abidjan", "Côte d’Ivoire - far western Africa; Iceland (GMT)" },
        {      0.0f, "GMT",        5.55f,     -0.2167f, "GH", "Africa/Accra", "Ghana" },
        {      0.0f, "GMT",       12.65f,        -8.0f, "ML", "Africa/Bamako", "Mali" },
        {      0.0f, "GMT",     13.4667f,      -16.65f, "GM", "Africa/Banjul", "Gambia" },
        {      0.0f, "GMT",       11.85f,    -15.5833f, "GW", "Africa/Bissau", "Guinea-Bissau" },
        {      0.0f, "GMT",      9.5167f,    -13.7167f, "GN", "Africa/Conakry", "Guinea" },
        {      0.0f, "GMT",     14.6667f,    -17.4333f, "SN", "Africa/Dakar", "Senegal" },
        {      0.0f, "GMT",         8.5f,      -13.25f, "SL", "Africa/Freetown", "Sierra Leone" },
        {      0.0f, "GMT",      6.1333f,      1.2167f, "TG", "Africa/Lome", "Togo" },
        {      0.0f, "GMT",         6.3f,    -10.7833f, "LR", "Africa/Monrovia", "Liberia" },
        {      0.0f, "GMT",        18.1f,      -15.95f, "MR", "Africa/Nouakchott", "Mauritania" },
        {      0.0f, "GMT",     12.3667f,     -1.5167f, "BF", "Africa/Ouagadougou", "Burkina Faso" },
        {      0.0f, "GMT",      0.3333f,      6.7333f, "ST", "Africa/Sao_Tome", "Sao Tome & Principe" },
        {      0.0f, "AST",        18.2f,    -63.0667f, "AI", "America/Anguilla", "Anguilla" },
        {      0.0f, "GMT",     76.7667f,    -18.6667f, "GL", "America/Danmarkshavn", "Greenland - National Park (east coast)" },
        {      0.0f, "GMT",    -72.0114f,       2.535f, "AQ", "Antarctica/Troll", "Antarctica - Troll" },
        {      0.0f, "WET",        28.1f,       -15.4f, "ES", "Atlantic/Canary", "Spain - Canary Islands" },
        {      0.0f, "WET",     62.0167f,     -6.7667f, "FO", "Atlantic/Faroe", "Faroe Islands" },
        {      0.0f, "WET",     32.6333f,       -16.9f, "PT", "Atlantic/Madeira", "Portugal - Madeira Islands" },
        {      0.0f, "GMT",       64.15f,      -21.85f, "IS", "Atlantic/Reykjavik", "Iceland" },
        {      10.0f, "AEST",    -15.9167f,        -5.7f, "SH", "Atlantic/St_Helena", "St Helena" },
        {      10.5f, "ACDT",      -31.95f,      141.45f, "AU", "Australia/Broken_Hill", "Australia - New South Wales (Yancowinna)" },
        {      10.0f, "AEST",    -42.8833f,    147.3167f, "AU", "Australia/Hobart", "Australia - Tasmania" },
        {      10.0f, "AEST",    -20.2667f,       149.0f, "AU", "Australia/Lindeman", "Australia - Queensland (Whitsunday Islands)" },
        {      10.0f, "AEST",    -37.8167f,    144.9667f, "AU", "Australia/Melbourne", "Australia - Victoria" },
        {      0.0f, "GMT",     49.4547f,     -2.5361f, "GG", "Europe/Guernsey", "Guernsey" },
        {      0.0f, "GMT",       54.15f,     -4.4667f, "IM", "Europe/Isle_of_Man", "Isle of Man" },
        {      0.0f, "GMT",     49.1836f,     -2.1067f, "JE", "Europe/Jersey", "Jersey" },
        {      0.0f, "WET",     38.7167f,     -9.1333f, "PT", "Europe/Lisbon", "Portugal - Portugal (mainland)" },
        {      0.0f, "GMT",     51.5083f,     -0.1253f, "GB", "Europe/London", "Britain (UK) - United Kingdom (GMT/BST)" },
        {      1.0f, "CET",     36.7833f,        3.05f, "DZ", "Africa/Algiers", "Algeria - Algeria, Tunisia (CET)" },
        {      1.0f, "WAT",      4.3667f,     18.5833f, "CF", "Africa/Bangui", "Central African Rep." },
        {      1.0f, "WAT",     -4.2667f,     15.2833f, "CG", "Africa/Brazzaville", "Congo (Rep.)" },
        {      1.0f, "WEST",       33.65f,     -7.5833f, "MA", "Africa/Casablanca", "Morocco - Morocco" },
        {      1.0f, "CET",     35.8833f,     -5.3167f, "ES", "Africa/Ceuta", "Spain - Ceuta, Melilla" },
        {      1.0f, "WAT",        4.05f,         9.7f, "CM", "Africa/Douala", "Cameroon" },
        {      1.0f, "WAT",       27.15f,       -13.2f, "EH", "Africa/El_Aaiun", "Western Sahara" },
        {      1.0f, "WAT",        -4.3f,        15.3f, "CD", "Africa/Kinshasa", "Congo (Dem. Rep.) - Dem. Rep. of Congo (west)" },
        {      1.0f, "WAT",        6.45f,         3.4f, "NG", "Africa/Lagos", "Nigeria - western Africa (WAT)" },
        {      1.0f, "WAT",      0.3833f,        9.45f, "GA", "Africa/Libreville", "Gabon" },
        {      1.0f, "WAT",        -8.8f,     13.2333f, "AO", "Africa/Luanda", "Angola" },
        {      1.0f, "WAT",        3.75f,      8.7833f, "GQ", "Africa/Malabo", "Equatorial Guinea" },
        {      1.0f, "WAT",     12.1167f,       15.05f, "TD", "Africa/Ndjamena", "Chad" },
        {      1.0f, "WAT",     13.5167f,      2.1167f, "NE", "Africa/Niamey", "Niger" },
        {      1.0f, "WAT",      6.4833f,      2.6167f, "BJ", "Africa/Porto-Novo", "Benin" },
        {      1.0f, "CET",        36.8f,     10.1833f, "TN", "Africa/Tunis", "Tunisia" },
        {      1.0f, "CET",        78.0f,        16.0f, "SJ", "Arctic/Longyearbyen", "Svalbard & Jan Mayen" },
        {      1.0f, "CET",     52.3667f,         4.9f, "NL", "Europe/Amsterdam", "Netherlands" },
        {      1.0f, "CET",        42.5f,      1.5167f, "AD", "Europe/Andorra", "Andorra" },
        {      1.0f, "CET",     44.8333f,        20.5f, "RS", "Europe/Belgrade", "Serbia" },
        {      1.0f, "CET",        52.5f,     13.3667f, "DE", "Europe/Berlin", "Germany - most of Germany" },
        {      1.0f, "CET",       48.15f,     17.1167f, "SK", "Europe/Bratislava", "Slovakia" },
        {      1.0f, "CET",     50.8333f,      4.3333f, "BE", "Europe/Brussels", "Belgium" },
        {      1.0f, "CET",        47.5f,     19.0833f, "HU", "Europe/Budapest", "Hungary" },
        {      1.0f, "CET",        47.7f,      8.6833f, "DE", "Europe/Busingen", "Germany - Busingen" },
        {      1.0f, "CET",     55.6667f,     12.5833f, "DK", "Europe/Copenhagen", "Denmark" },
        {      1.0f, "IST",     53.3333f,       -6.25f, "IE", "Europe/Dublin", "Ireland - Ireland (IST/GMT)" },
        {      1.0f, "CET",     36.1333f,       -5.35f, "GI", "Europe/Gibraltar", "Gibraltar" },
        {      1.0f, "CET",       46.05f,     14.5167f, "SI", "Europe/Ljubljana", "Slovenia" },
        {      1.0f, "CET",        49.6f,        6.15f, "LU", "Europe/Luxembourg", "Luxembourg" },
        {      1.0f, "CET",        40.4f,     -3.6833f, "ES", "Europe/Madrid", "Spain - Spain (mainland)" },
        {      1.0f, "CET",        35.9f,     14.5167f, "MT", "Europe/Malta", "Malta" },
        {      1.0f, "CET",        43.7f,      7.3833f, "MC", "Europe/Monaco", "Monaco" },
        {      1.0f, "CET",     59.9167f,       10.75f, "NO", "Europe/Oslo", "Norway" },
        {      1.0f, "CET",     48.8667f,      2.3333f, "FR", "Europe/Paris", "France - central Europe (CET/CEST)" },
        {      1.0f, "CET",     42.4333f,     19.2667f, "ME", "Europe/Podgorica", "Montenegro" },
        {      1.0f, "CET",     50.0833f,     14.4333f, "CZ", "Europe/Prague", "Czech Republic" },
        {      1.0f, "CET",        41.9f,     12.4833f, "IT", "Europe/Rome", "Italy" },
        {      1.0f, "CET",     43.9167f,     12.4667f, "SM", "Europe/San_Marino", "San Marino" },
        {      1.0f, "CET",     43.8667f,     18.4167f, "BA", "Europe/Sarajevo", "Bosnia & Herzegovina" },
        {      1.0f, "CET",     41.9833f,     21.4333f, "MK", "Europe/Skopje", "North Macedonia" },
        {      1.0f, "CET",     59.3333f,       18.05f, "SE", "Europe/Stockholm", "Sweden" },
        {      1.0f, "CET",     41.3333f,     19.8333f, "AL", "Europe/Tirane", "Albania" },
        {      1.0f, "CET",       47.15f,      9.5167f, "LI", "Europe/Vaduz", "Liechtenstein" },
        {      1.0f, "CET",     41.9022f,     12.4531f, "VA", "Europe/Vatican", "Vatican City" },
        {      1.0f, "CET",     48.2167f,     16.3333f, "AT", "Europe/Vienna", "Austria" },
        {      1.0f, "CET",       52.25f,        21.0f, "PL", "Europe/Warsaw", "Poland" },
        {      1.0f, "CET",        45.8f,     15.9667f, "HR", "Europe/Zagreb", "Croatia" },
        {      1.0f, "CET",     47.3833f,      8.5333f, "CH", "Europe/Zurich", "Switzerland" },
        {      2.0f, "EET",     60.1667f,     24.9667f, "FI", "Europe/Helsinki", "Finland" },
        {      2.0f, "CAT",    -15.7833f,        35.0f, "MW", "Africa/Blantyre", "Malawi" },
        {      2.0f, "CAT",     -3.3833f,     29.3667f, "BI", "Africa/Bujumbura", "Burundi" },
        {      2.0f, "EET",       30.05f,       31.25f, "EG", "Africa/Cairo", "Egypt - Egypt" },
        {      2.0f, "CAT",      -24.65f,     25.9167f, "BW", "Africa/Gaborone", "Botswana" },
        {      2.0f, "CAT",    -17.8333f,       31.05f, "ZW", "Africa/Harare", "Zimbabwe" },
        {      2.0f, "SAST",      -26.25f,        28.0f, "ZA", "Africa/Johannesburg", "South Africa - southern Africa (SAST)" },
        {      2.0f, "CAT",        4.85f,     31.6167f, "SS", "Africa/Juba", "South Sudan" },
        {      2.0f, "CAT",       -1.95f,     30.0667f, "RW", "Africa/Kigali", "Rwanda" },
        {      2.0f, "CAT",    -11.6667f,     27.4667f, "CD", "Africa/Lubumbashi", "Congo (Dem. Rep.) - Dem. Rep. of Congo (east)" },
        {      2.0f, "CAT",    -15.4167f,     28.2833f, "ZM", "Africa/Lusaka", "Zambia" },
        {      2.0f, "CAT",    -25.9667f,     32.5833f, "MZ", "Africa/Maputo", "Mozambique - central Africa (CAT)" },
        {      2.0f, "SAST",    -29.4667f,        27.5f, "LS", "Africa/Maseru", "Lesotho" },
        {      2.0f, "SAST",       -26.3f,        31.1f, "SZ", "Africa/Mbabane", "Eswatini (Swaziland)" },
        {      2.0f, "EET",        32.9f,     13.1833f, "LY", "Africa/Tripoli", "Libya - Libya; Kaliningrad (EET)" },
        {      2.0f, "CAT",    -22.5667f,        17.1f, "NA", "Africa/Windhoek", "Namibia" },
        {      2.0f, "EET",     33.8833f,        35.5f, "LB", "Asia/Beirut", "Lebanon - Lebanon" },
        {      2.0f, "EET",     35.1167f,       33.95f, "CY", "Asia/Famagusta", "Cyprus - Northern Cyprus" },
        {      2.0f, "EET",        31.5f,     34.4667f, "PS", "Asia/Gaza", "Palestine - Gaza Strip" },
        {      2.0f, "EET",     31.5333f,      35.095f, "PS", "Asia/Hebron", "Palestine - West Bank" },
        {      2.0f, "IST",     31.7806f,     35.2239f, "IL", "Asia/Jerusalem", "Israel - Israel" },
        {      2.0f, "EET",     35.1667f,     33.3667f, "CY", "Asia/Nicosia", "Cyprus - most of Cyprus" },
        {      2.0f, "EET",     37.9667f,     23.7167f, "GR", "Europe/Athens", "Greece - eastern Europe (EET/EEST)" },
        {      2.0f, "EET",     44.4333f,        26.1f, "RO", "Europe/Bucharest", "Romania" },
        {      2.0f, "EET",        47.0f,     28.8333f, "MD", "Europe/Chisinau", "Moldova - Moldova" },
        {      2.0f, "EET",     54.7167f,        20.5f, "RU", "Europe/Kaliningrad", "Russia - MSK-01 - Kaliningrad" },
        {      2.0f, "EET",     50.4333f,     30.5167f, "UA", "Europe/Kyiv", "Ukraine - most of Ukraine" },
        {      2.0f, "EET",        60.1f,       19.95f, "AX", "Europe/Mariehamn", "Åland Islands" },
        {      2.0f, "EET",       56.95f,        24.1f, "LV", "Europe/Riga", "Latvia" },
        {      2.0f, "EET",     42.6833f,     23.3167f, "BG", "Europe/Sofia", "Bulgaria" },
        {      2.0f, "EET",     59.4167f,       24.75f, "EE", "Europe/Tallinn", "Estonia" },
        {      2.0f, "EET",     54.6833f,     25.3167f, "LT", "Europe/Vilnius", "Lithuania" },
        {      3.0f, "EAT",      9.0333f,        38.7f, "ET", "Africa/Addis_Ababa", "Ethiopia" },
        {      3.0f, "EAT",     15.3333f,     38.8833f, "ER", "Africa/Asmara", "Eritrea" },
        {      3.0f, "EAT",        -6.8f,     39.2833f, "TZ", "Africa/Dar_es_Salaam", "Tanzania" },
        {      3.0f, "EAT",        11.6f,       43.15f, "DJ", "Africa/Djibouti", "Djibouti" },
        {      3.0f, "EAT",      0.3167f,     32.4167f, "UG", "Africa/Kampala", "Uganda" },
        {      3.0f, "CAT",        15.6f,     32.5333f, "SD", "Africa/Khartoum", "Sudan" },
        {      3.0f, "EAT",      2.0667f,     45.3667f, "SO", "Africa/Mogadishu", "Somalia" },
        {      3.0f, "EAT",     -1.2833f,     36.8167f, "KE", "Africa/Nairobi", "Kenya - eastern Africa (EAT)" },
        {      3.0f, "SYOT",    -69.0061f,       39.59f, "AQ", "Antarctica/Syowa", "Antarctica - Syowa" },
        {      3.0f, "AST",       12.75f,        45.2f, "YE", "Asia/Aden", "Yemen" },
        {      3.0f, "EEST",       31.95f,     35.9333f, "JO", "Asia/Amman", "Jordan" },
        {      3.0f, "AST",       33.35f,     44.4167f, "IQ", "Asia/Baghdad", "Iraq" },
        {      3.0f, "AST",     26.3833f,     50.5833f, "BH", "Asia/Bahrain", "Bahrain" },
        {      3.0f, "EEST",        33.5f,        36.3f, "SY", "Asia/Damascus", "Syria" },
        {      3.0f, "AST",     29.3333f,     47.9833f, "KW", "Asia/Kuwait", "Kuwait" },
        {      3.0f, "AST",     25.2833f,     51.5333f, "QA", "Asia/Qatar", "Qatar" },
        {      3.0f, "AST",     24.6333f,     46.7167f, "SA", "Asia/Riyadh", "Saudi Arabia" },
        {      3.0f, "TRT",     41.0167f,     28.9667f, "TR", "Europe/Istanbul", "Turkey - Near East; Belarus" },
        {      3.0f, "MSK",        58.6f,       49.65f, "RU", "Europe/Kirov", "Russia - MSK+00 - Kirov" },
        {      3.0f, "MSK",        53.9f,     27.5667f, "BY", "Europe/Minsk", "Belarus" },
        {      3.0f, "MSK",     55.7558f,     37.6178f, "RU", "Europe/Moscow", "Russia - MSK+00 - Moscow area" },
        {      3.0f, "MSK",       44.95f,        34.1f, "UA", "Europe/Simferopol", "Ukraine - Crimea" },
        {      3.0f, "MSK",     48.7333f,     44.4167f, "RU", "Europe/Volgograd", "Russia - MSK+00 - Volgograd" },
        {      3.0f, "EAT",    -18.9167f,     47.5167f, "MG", "Indian/Antananarivo", "Madagascar" },
        {      3.0f, "EAT",    -11.6833f,     43.2667f, "KM", "Indian/Comoro", "Comoros" },
        {      3.0f, "EAT",    -12.7833f,     45.2333f, "YT", "Indian/Mayotte", "Mayotte" },
        {      3.5f, "IRST",     35.6667f,     51.4333f, "IR", "Asia/Tehran", "Iran - Iran" },
        {      4.0f, "AZT",     40.3833f,       49.85f, "AZ", "Asia/Baku", "Azerbaijan" },
        {      4.0f, "GST",        25.3f,        55.3f, "AE", "Asia/Dubai", "United Arab Emirates - Russia; Caucasus; Persian Gulf; Seychelles; Réunion" },
        {      4.0f, "GST",        23.6f,     58.5833f, "OM", "Asia/Muscat", "Oman" },
        {      4.0f, "GET",     41.7167f,     44.8167f, "GE", "Asia/Tbilisi", "Georgia" },
        {      4.0f, "AMT",     40.1833f,        44.5f, "AM", "Asia/Yerevan", "Armenia" },
        {      4.0f, "MSK",       46.35f,       48.05f, "RU", "Europe/Astrakhan", "Russia - MSK+01 - Astrakhan" },
        {      4.0f, "MSK",        53.2f,       50.15f, "RU", "Europe/Samara", "Russia - MSK+01 - Samara, Udmurtia" },
        {      4.0f, "MSK",     51.5667f,     46.0333f, "RU", "Europe/Saratov", "Russia - MSK+01 - Saratov" },
        {      4.0f, "MSK",     54.3333f,        48.4f, "RU", "Europe/Ulyanovsk", "Russia - MSK+01 - Ulyanovsk" },
        {      4.0f, "SCT",     -4.6667f,     55.4667f, "SC", "Indian/Mahe", "Seychelles" },
        {      4.0f, "MUT",    -20.1667f,        57.5f, "MU", "Indian/Mauritius", "Mauritius" },
        {      4.0f, "RET",    -20.8667f,     55.4667f, "RE", "Indian/Reunion", "Réunion" },
        {      4.5f, "AFT",     34.5167f,        69.2f, "AF", "Asia/Kabul", "Afghanistan - Afghanistan" },
        {      5.0f, "MAWT",       -67.6f,     62.8833f, "AQ", "Antarctica/Mawson", "Antarctica - Mawson" },
        {      5.0f, "VOST",       -78.4f,       106.9f, "AQ", "Antarctica/Vostok", "Antarctica - Vostok" },
        {      5.0f, "AQTT",     44.5167f,     50.2667f, "KZ", "Asia/Aqtau", "Kazakhstan - Mangghystau/Mankistau" },
        {      5.0f, "AQTT",     50.2833f,     57.1667f, "KZ", "Asia/Aqtobe", "Kazakhstan - Aqtobe/Aktobe" },
        {      5.0f, "TMT",       37.95f,     58.3833f, "TM", "Asia/Ashgabat", "Turkmenistan" },
        {      5.0f, "AQTT",     47.1167f,     51.9333f, "KZ", "Asia/Atyrau", "Kazakhstan - Atyrau/Atirau/Gur'yev" },
        {      5.0f, "TJT",     38.5833f,        68.8f, "TJ", "Asia/Dushanbe", "Tajikistan" },
        {      5.0f, "PKT",     24.8667f,       67.05f, "PK", "Asia/Karachi", "Pakistan - Pakistan (PKT)" },
        {      5.0f, "ORAT",     51.2167f,       51.35f, "KZ", "Asia/Oral", "Kazakhstan - West Kazakhstan" },
        {      5.0f, "ORAT",        44.8f,     65.4667f, "KZ", "Asia/Qyzylorda", "Kazakhstan - Qyzylorda/Kyzylorda/Kzyl-Orda" },
        {      5.0f, "UZT",     39.6667f,        66.8f, "UZ", "Asia/Samarkand", "Uzbekistan - Uzbekistan (west)" },
        {      5.0f, "UZT",     41.3333f,        69.3f, "UZ", "Asia/Tashkent", "Uzbekistan - Uzbekistan (east)" },
        {      5.0f, "YEKT",       56.85f,        60.6f, "RU", "Asia/Yekaterinburg", "Russia - MSK+02 - Urals" },
        {      5.0f, "TFT",    -49.3528f,     70.2175f, "TF", "Indian/Kerguelen", "French S. Terr." },
        {      5.0f, "MVT",      4.1667f,        73.5f, "MV", "Indian/Maldives", "Maldives" },
        {      5.5f, "IST",      6.9333f,       79.85f, "LK", "Asia/Colombo", "Sri Lanka - Sri Lanka" },
        {      5.5f, "IST",     22.5333f,     88.3667f, "IN", "Asia/Kolkata", "India - India (IST)" },
        {     5.75f, "NPT",     27.7167f,     85.3167f, "NP", "Asia/Kathmandu", "Nepal - Nepal" },
        {      6.0f, "AQTT",       43.25f,       76.95f, "KZ", "Asia/Almaty", "Kazakhstan - most of Kazakhstan" },
        {      6.0f, "KGT",        42.9f,        74.6f, "KG", "Asia/Bishkek", "Kyrgyzstan" },
        {      6.0f, "BST",     23.7167f,     90.4167f, "BD", "Asia/Dhaka", "Bangladesh - Russia; Kyrgyzstan; Bhutan; Bangladesh; Chagos" },
        {      6.0f, "OMST",        55.0f,        73.4f, "RU", "Asia/Omsk", "Russia - MSK+03 - Omsk" },
        {      6.0f, "AQTT",        53.2f,     63.6167f, "KZ", "Asia/Qostanay", "Kazakhstan - Qostanay/Kostanay/Kustanay" },
        {      6.0f, "BTT",     27.4667f,       89.65f, "BT", "Asia/Thimphu", "Bhutan" },
        {      6.0f, "CST",        43.8f,     87.5833f, "CN", "Asia/Urumqi", "China - Xinjiang Time" },
        {      6.0f, "IOT",     -7.3333f,     72.4167f, "IO", "Indian/Chagos", "British Indian Ocean Territory" },
        {      6.5f, "MMT",     16.7833f,     96.1667f, "MM", "Asia/Yangon", "Myanmar (Burma) - Myanmar; Cocos" },
        {      6.5f, "CCT",    -12.1667f,     96.9167f, "CC", "Indian/Cocos", "Cocos (Keeling) Islands" },
        {      7.0f, "DAVT",    -68.5833f,     77.9667f, "AQ", "Antarctica/Davis", "Antarctica - Davis" },
        {      7.0f, "ICT",       13.75f,    100.5167f, "TH", "Asia/Bangkok", "Thailand - Russia; Indochina; Christmas Island" },
        {      7.0f, "MSK",     53.3667f,       83.75f, "RU", "Asia/Barnaul", "Russia - MSK+04 - Altai" },
        {      7.0f, "ICT",       10.75f,    106.6667f, "VN", "Asia/Ho_Chi_Minh", "Vietnam" },
        {      7.0f, "HOVT",     48.0167f,       91.65f, "MN", "Asia/Hovd", "Mongolia - Bayan-Olgii, Hovd, Uvs" },
        {      7.0f, "WIB",     -6.1667f,       106.8f, "ID", "Asia/Jakarta", "Indonesia - Java, Sumatra" },
        {      7.0f, "KRAT",     56.0167f,     92.8333f, "RU", "Asia/Krasnoyarsk", "Russia - MSK+04 - Krasnoyarsk area" },
        {      7.0f, "KRAT",       53.75f,     87.1167f, "RU", "Asia/Novokuznetsk", "Russia - MSK+04 - Kemerovo" },
        {      7.0f, "NOVT",     55.0333f,     82.9167f, "RU", "Asia/Novosibirsk", "Russia - MSK+04 - Novosibirsk" },
        {      7.0f, "ICT",       11.55f,    104.9167f, "KH", "Asia/Phnom_Penh", "Cambodia" },
        {      7.0f, "WIB",     -0.0333f,    109.3333f, "ID", "Asia/Pontianak", "Indonesia - Borneo (west, central)" },
        {      7.0f, "MSK",        56.5f,     84.9667f, "RU", "Asia/Tomsk", "Russia - MSK+04 - Tomsk" },
        {      7.0f, "ICT",     17.9667f,       102.6f, "LA", "Asia/Vientiane", "Laos" },
        {      7.0f, "CXT",    -10.4167f,    105.7167f, "CX", "Indian/Christmas", "Christmas Island" },
        {      8.0f, "BNT",      4.9333f,    114.9167f, "BN", "Asia/Brunei", "Brunei" },
        {      8.0f, "HKT",     22.2833f,      114.15f, "HK", "Asia/Hong_Kong", "Hong Kong - Hong Kong (HKT)" },
        {      8.0f, "IRKT",     52.2667f,    104.3333f, "RU", "Asia/Irkutsk", "Russia - MSK+05 - Irkutsk, Buryatia" },
        {      8.0f, "MYT",      3.1667f,       101.7f, "MY", "Asia/Kuala_Lumpur", "Malaysia - Malaysia (peninsula)" },
        {      8.0f, "MYT",        1.55f,    110.3333f, "MY", "Asia/Kuching", "Malaysia - Sabah, Sarawak" },
        {      8.0f, "CST",     22.1972f,    113.5417f, "MO", "Asia/Macau", "Macau" },
        {      8.0f, "WITA",     -5.1167f,       119.4f, "ID", "Asia/Makassar", "Indonesia - Borneo (east, south), Sulawesi/Celebes, Bali, Nusa Tengarra, Timor (west)" },
        {      8.0f, "PHST",     14.5867f,    120.9678f, "PH", "Asia/Manila", "Philippines - Philippines (PHT)" },
        {      8.0f, "CST",     31.2333f,    121.4667f, "CN", "Asia/Shanghai", "China - Beijing Time" },
        {      8.0f, "SGT",      1.2833f,      103.85f, "SG", "Asia/Singapore", "Singapore - Russia; Brunei; Malaysia; Singapore; Concordia" },
        {      8.0f, "CST",       25.05f,       121.5f, "TW", "Asia/Taipei", "Taiwan" },
        {      8.0f, "ULAT",     47.9167f,    106.8833f, "MN", "Asia/Ulaanbaatar", "Mongolia - most of Mongolia" },
        {      8.0f, "AWST",      -31.95f,      115.85f, "AU", "Australia/Perth", "Australia - Western Australia (most areas)" },
        {     8.0f, "CAST",    -66.2833f,    110.5167f, "AQ", "Antarctica/Casey", "Antarctica - Casey" },
        {     8.75f, "ACWST",    -31.7167f,    128.8667f, "AU", "Australia/Eucla", "Australia - Western Australia (Eucla)" },
        {      9.0f, "YAKT",       52.05f,    113.4667f, "RU", "Asia/Chita", "Russia - MSK+06 - Zabaykalsky" },
        {      9.0f, "TLT",       -8.55f,    125.5833f, "TL", "Asia/Dili", "East Timor" },
        {      9.0f, "WIT",     -2.5333f,       140.7f, "ID", "Asia/Jayapura", "Indonesia - New Guinea (West Papua / Irian Jaya), Malukus/Moluccas" },
        {      9.0f, "YAKT",     62.6564f,    135.5539f, "RU", "Asia/Khandyga", "Russia - MSK+06 - Tomponsky, Ust-Maysky" },
        {      9.0f, "KST",     39.0167f,      125.75f, "KP", "Asia/Pyongyang", "Korea (North)" },
        {      9.0f, "KST",       37.55f,    126.9667f, "KR", "Asia/Seoul", "Korea (South) - Korea (KST)" },
        {      9.0f, "JST",     35.6544f,    139.7447f, "JP", "Asia/Tokyo", "Japan - Japan (JST); Eyre Bird Observatory" },
        {      9.0f, "YAKT",        62.0f,    129.6667f, "RU", "Asia/Yakutsk", "Russia - MSK+06 - Lena River" },
        {      9.0f, "PWT",      7.3333f,    134.4833f, "PW", "Pacific/Palau", "Palau" },
        {      9.5f, "ACST",    -34.9167f,    138.5833f, "AU", "Australia/Adelaide", "Australia - South Australia" },
        {      9.5f, "ACST",    -12.4667f,    130.8333f, "AU", "Australia/Darwin", "Australia - Northern Territory" },
        {     10.0f, "DDUT",    -66.6667f,    140.0167f, "AQ", "Antarctica/DumontDUrville", "Antarctica - Dumont-d'Urville" },
        {     10.0f, "VLAT",     64.5603f,    143.2267f, "RU", "Asia/Ust-Nera", "Russia - MSK+07 - Oymyakonsky" },
        {     10.0f, "VLAT",     43.1667f,    131.9333f, "RU", "Asia/Vladivostok", "Russia - MSK+07 - Amur River" },
        {     10.0f, "AEST",    -27.4667f,    153.0333f, "AU", "Australia/Brisbane", "Australia - Queensland (most areas)" },
        {     10.0f, "AEST",    -33.8667f,    151.2167f, "AU", "Australia/Sydney", "Australia - New South Wales (most areas)" },
        {     10.0f, "CHUT",      7.4167f,    151.7833f, "FM", "Pacific/Chuuk", "Micronesia - Chuuk/Truk, Yap" },
        {     10.0f, "ChST",     13.4667f,      144.75f, "GU", "Pacific/Guam", "Guam - Mariana Islands (ChST)" },
        {     10.0f, "PGT",        -9.5f,    147.1667f, "PG", "Pacific/Port_Moresby", "Papua New Guinea - most of Papua New Guinea" },
        {     10.0f, "ChST",        15.2f,      145.75f, "MP", "Pacific/Saipan", "Northern Mariana Islands" },
        {     10.5f, "LHST",      -31.55f,    159.0833f, "AU", "Australia/Lord_Howe", "Australia - Lord Howe Island" },
        {     11.0f, "AEDT",       -54.5f,      158.95f, "AU", "Antarctica/Macquarie", "Australia - Macquarie Island" },
        {     11.0f, "MAGT",     59.5667f,       150.8f, "RU", "Asia/Magadan", "Russia - MSK+08 - Magadan" },
        {     11.0f, "SAKT",     46.9667f,       142.7f, "RU", "Asia/Sakhalin", "Russia - MSK+08 - Sakhalin Island" },
        {     11.0f, "SRET",     67.4667f,    153.7167f, "RU", "Asia/Srednekolymsk", "Russia - MSK+08 - Sakha (E), N Kuril Is" },
        {     11.0f, "BST",     -6.2167f,    155.5667f, "PG", "Pacific/Bougainville", "Papua New Guinea - Bougainville" },
        {     11.0f, "VUT",    -17.6667f,    168.4167f, "VU", "Pacific/Efate", "Vanuatu" },
        {     11.0f, "SBT",     -9.5333f,       160.2f, "SB", "Pacific/Guadalcanal", "Solomon Islands" },
        {     11.0f, "KOST",      5.3167f,    162.9833f, "FM", "Pacific/Kosrae", "Micronesia - Kosrae" },
        {     11.0f, "NFDT",      -29.05f,    167.9667f, "NF", "Pacific/Norfolk", "Norfolk Island - Norfolk Island" },
        {     11.0f, "NCT",    -22.2667f,      166.45f, "NC", "Pacific/Noumea", "New Caledonia" },
        {     11.0f, "PONT",      6.9667f,    158.2167f, "FM", "Pacific/Pohnpei", "Micronesia - Pohnpei/Ponape" },
        {     12.0f, "NZST",    -77.8333f,       166.6f, "AQ", "Antarctica/McMurdo", "Antarctica - New Zealand time - McMurdo, South Pole" },
        {     12.0f, "ANAT",       64.75f,    177.4833f, "RU", "Asia/Anadyr", "Russia - MSK+09 - Bering Sea" },
        {     12.0f, "PETT",     53.0167f,      158.65f, "RU", "Asia/Kamchatka", "Russia - MSK+09 - Kamchatka" },
        {     12.0f, "NZST",    -36.8667f,    174.7667f, "NZ", "Pacific/Auckland", "New Zealand - most of New Zealand" },
        {     12.0f, "FJT",    -18.1333f,    178.4167f, "FJ", "Pacific/Fiji", "Fiji" },
        {     12.0f, "TVT",     -8.5167f,    179.2167f, "TV", "Pacific/Funafuti", "Tuvalu" },
        {     12.0f, "MHT",      9.0833f,    167.3333f, "MH", "Pacific/Kwajalein", "Marshall Islands - Kwajalein" },
        {     12.0f, "MHT",        7.15f,       171.2f, "MH", "Pacific/Majuro", "Marshall Islands - most of Marshall Islands" },
        {     12.0f, "NRT",     -0.5167f,    166.9167f, "NR", "Pacific/Nauru", "Nauru" },
        {     12.0f, "GILT",      1.4167f,       173.0f, "KI", "Pacific/Tarawa", "Kiribati - Gilbert Islands" },
        {     12.0f, "WAKT",     19.2833f,    166.6167f, "UM", "Pacific/Wake", "US minor outlying islands - Wake Island" },
        {     12.0f, "WFT",       -13.3f,   -176.1667f, "WF", "Pacific/Wallis", "Wallis & Futuna" },
        {    12.75f, "CHAST",      -43.95f,     -176.55f, "NZ", "Pacific/Chatham", "New Zealand - Chatham Islands" },
        {     13.0f, "SST",    -13.8333f,   -171.7333f, "WS", "Pacific/Apia", "Samoa (western)" },
        {     13.0f, "TKT",     -9.3667f,   -171.2333f, "TK", "Pacific/Fakaofo", "Tokelau" },
        {     13.0f, "PHOT",     -2.7833f,   -171.7167f, "KI", "Pacific/Kanton", "Kiribati - Phoenix Islands" },
        {     13.0f, "TOT",    -21.1333f,      -175.2f, "TO", "Pacific/Tongatapu", "Tonga - Kanton; Tokelau; Samoa (western); Tonga" },
        {     14.0f, "LINT",      1.8667f,   -157.3333f, "KI", "Pacific/Kiritimati", "Kiribati - Line Islands" },
    };

    // ── lookup helpers ────────────────────────────────────────────────────

    // find the first entry whose tz_code matches exactly (nullptr if not found)
    const TimeZoneDbEntry* TimeZone::FindByZoneCode(const char* zone_code)
    {
        for (const auto& i : k_timezone_db)
        {
            if (std::strcmp(i.tz_code, zone_code) == 0)
            {
                return &i;
            }
        }
        return nullptr;
    }

    // find the first entry whose country_code matches exactly (nullptr if not found)
    const TimeZoneDbEntry* TimeZone::FindByCountryCode(const char* cc)
    {
        for (const auto& i : k_timezone_db)
        {
            if (std::strcmp(i.country_code, cc) == 0)
            {
                return &i;
            }
        }
        return nullptr;
    }

    // return all entries that share the given UTC offset (within epsilon)
    std::vector<const TimeZoneDbEntry*> TimeZone::FindByOffset(float offset_hours, float epsilon)
    {
        std::vector<const TimeZoneDbEntry*> results;
        for (const auto& i : k_timezone_db)
        {
            if (std::fabs(i.offset_hours - offset_hours) < epsilon)
            {
                results.push_back(&i);
            }
        }
        return results;
    }

    // return all entries for a given country code
    std::vector<const TimeZoneDbEntry*> TimeZone::FindAllByCountry(const char* cc)
    {
        std::vector<const TimeZoneDbEntry*> results;
        for (const auto& i : k_timezone_db)
        {
            if (std::strcmp(i.country_code, cc) == 0)
            {
                results.push_back(&i);
            }
        }
        return results;
    }

    // find the entry closest to a given lat/lon (Euclidean approximation)
    const TimeZoneDbEntry* TimeZone::FindNearest(float latitude, float longitude)
    {
        const TimeZoneDbEntry* best = nullptr;
        float best_dist = 1e30f;
        for (const auto& i : k_timezone_db)
        {
            float dlat = i.latitude  - latitude;
            float dlon = i.longitude - longitude;
            float dist = dlat * dlat + dlon * dlon;
            if (dist < best_dist)
            {
                best_dist = dist;
                best = &i;
            }
        }
        return best;
    }

} // namespace spartan
