//========================================================================
//
// pdftoopvp.cc
//
// Copyright 2005 AXE,Inc.
//
// 2007,2008,2009 Modified by BBR Inc.
//========================================================================

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <math.h>
#ifdef HAVE_CPP_POPPLER_VERSION_H
#include "cpp/poppler-version.h"
#endif
#if POPPLER_VERSION_MINOR > 72
typedef unsigned char Guchar;
#endif
#include "goo/GooString.h"
#define GSTRING_H // for old GooString.h
#include "goo/gmem.h"
#include "GlobalParams.h"
#include "Object.h"
#include "PDFDoc.h"
#include "splash/SplashBitmap.h"
#include "OPRS.h"
#include "OPVPOutputDev.h"
#include "Gfx.h"
#include <cups/cups.h>
#include <cups/ppd.h>
#include "OPVPError.h"

#define MMPERINCH (25.4)

static int resolution = 300;
static int hResolution = 0;
static int vResolution = 0;
static bool mono = false;
static bool reverse = false;
static bool gray = false;
static char enableFreeTypeStr[16] = "";
static bool quiet = false;
static char outputOrderStr[256] = "";
static bool oldLipsDriver = false;
static bool HPDriver = false;
static bool NECDriver = false;
static bool clipPathNotSaved = false;
static bool noShearImage = false;
static bool noLineStyle = false;
static bool noImageMask = false;
static bool noClipPath = false;
static bool ignoreMiterLimit = false;
static bool noMiterLimit = false;
static char printerDriver[1024] = "";
static char printerModel[1024] = "";
static char jobInfo[4096] = "";
static char docInfo[1024] = "";
static char pageInfo[1024] = "";
static bool noBitmapChar = false;
static char bitmapCharThreshold[20] = "2000";
static char maxClipPathLength[20] = "2000";
static char maxFillPathLength[20] = "4000";
static int pageWidth = -1;
static int pageHeight = -1;

static int outOnePage(PDFDoc *doc, OPVPOutputDev *opvpOut, int pg)
{
  char buf[1024];
  char *p;
  double pw = doc->getPageMediaWidth(pg);
  double ph = doc->getPageMediaHeight(pg);
  int paperWidth;
  int paperHeight;

  if (pw != pageWidth || ph != pageHeight) {
    if (pageInfo[0] != '\0') {
      snprintf(buf,sizeof(buf),"%s;MediaSize=%dx%dmm",pageInfo,
       (int)(pw*MMPERINCH/72),
       (int)(ph*MMPERINCH/72));
    } else {
      snprintf(buf,sizeof(buf),"MediaSize=%dx%dmm",
       (int)(pw*MMPERINCH/72),
       (int)(ph*MMPERINCH/72));
    }
    p = buf;
  } else {
    pw = pageWidth;
    ph = pageHeight;
    p = pageInfo;
  }

  paperWidth = (int)(pw*hResolution/72+0.5);
  paperHeight = (int)(ph*vResolution/72+0.5);
  if (opvpOut->OPVPStartPage(p,paperWidth,paperHeight) < 0) {
      opvpError(-1,"Start Page failed");
      return 2;
  }
  opvpOut->setScale(1.0,1.0,0,0,0,0,paperHeight);
  doc->displayPage(opvpOut, pg, resolution, resolution,
    0, true, true, false);
  if (opvpOut->outSlice() < 0) {
    opvpError(-1,"OutSlice failed");
    return 2;
  }
  if (opvpOut->OPVPEndPage() < 0) {
      opvpError(-1,"End Page failed");
      return 2;
  }
  return 0;
}

#define MAX_OPVP_OPTIONS 20

#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 23
void CDECL myErrorFun(void *data, ErrorCategory category,
#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 70
    Goffset pos, const char *msg)
#else
    Goffset pos, char *msg)
#endif /* MAJOR > 0 || MINOR >= 70 */
#else
void CDECL myErrorFun(void *data, ErrorCategory category,
    int pos, char *msg)
#endif
{
  if (pos >= 0) {
#if POPPLER_VERSION_MAJOR > 0 || POPPLER_VERSION_MINOR >= 23
    fprintf(stderr, "ERROR (%lld): ", pos);
#else
    fprintf(stderr, "ERROR (%d): ", pos);
#endif
  } else {
    fprintf(stderr, "ERROR: ");
  }
  fprintf(stderr, "%s\n",msg);
  fflush(stderr);
}

static bool getColorProfilePath(ppd_file_t *ppd, GooString *path)
{
    // get color profile path
    const char *colorModel;
    const char *cupsICCQualifier2;
    const char *cupsICCQualifier2Choice;
    const char *cupsICCQualifier3;
    const char *cupsICCQualifier3Choice;
    ppd_attr_t *attr;
    ppd_choice_t *choice;
    const char *datadir;

    if ((attr = ppdFindAttr(ppd,"ColorModel",NULL)) != NULL) {
	colorModel = attr->value;
    } else {
	colorModel = NULL;
    }
    if ((attr = ppdFindAttr(ppd,"cupsICCQualifier2",NULL)) != NULL) {
	cupsICCQualifier2 = attr->value;
    } else {
	cupsICCQualifier2 = "MediaType";
    }
    if ((choice = ppdFindMarkedChoice(ppd,cupsICCQualifier2)) != NULL) {
	cupsICCQualifier2Choice = choice->choice;
    } else {
	cupsICCQualifier2Choice = NULL;
    }
    if ((attr = ppdFindAttr(ppd,"cupsICCQualifier3",NULL)) != NULL) {
	cupsICCQualifier3 = attr->value;
    } else {
	cupsICCQualifier3 = "Resolution";
    }
    if ((choice = ppdFindMarkedChoice(ppd,cupsICCQualifier3)) != NULL) {
	cupsICCQualifier3Choice = choice->choice;
    } else {
	cupsICCQualifier3Choice = NULL;
    }

    for (attr = ppdFindAttr(ppd,"cupsICCProfile",NULL);attr != NULL;
       attr = ppdFindNextAttr(ppd,"cupsICCProfile",NULL)) {
	// check color model
	char buf[PPD_MAX_NAME];
	char *p, *r;

	strncpy(buf,attr->spec,sizeof(buf));
	if ((p = strchr(buf,'.')) != NULL) {
	    *p = '\0';
	}
	if (colorModel != NULL && buf[0] != '\0'
	    && strcasecmp(buf,colorModel) != 0) continue;
	if (p == NULL) {
	    break;
	} else {
	    p++;
	    if ((r = strchr(p,'.')) != 0) {
		*r = '\0';
	    }
	}
	if (cupsICCQualifier2Choice != NULL && p[0] != '\0'
	    && strcasecmp(p,cupsICCQualifier2Choice) != 0) continue;
	if (r == NULL) {
	    break;
	} else {
	    r++;
	    if ((p = strchr(r,'.')) != 0) {
		*p = '\0';
	    }
	}
	if (cupsICCQualifier3Choice == NULL || r[0] == '\0'
	    || strcasecmp(r,cupsICCQualifier3Choice) == 0) break;
    }
    if (attr != NULL) {
	// matched
	path->clear();
	if (attr->value[0] != '/') {
	    if ((datadir = getenv("CUPS_DATADIR")) == NULL)
		datadir = CUPS_DATADIR;
	    path->append(datadir);
	    path->append("/profiles/");
	}
	path->append(attr->value);
	return true;
    }
    return false;
}

int main(int argc, char *argv[]) {
/* mtrace(); */
  int exitCode;
{
  PDFDoc *doc;
  SplashColor paperColor;
  OPVPOutputDev *opvpOut;
  bool ok = true;
  int pg;
  const char *optionKeys[MAX_OPVP_OPTIONS];
  const char *optionVals[MAX_OPVP_OPTIONS];
  int nOptions = 0;
  int numPages;
  int i;
  GooString fileName;
  GooString colorProfilePath("opvp.icc");

  exitCode = 99;
  setErrorCallback(::myErrorFun,NULL);

  // parse args
  int num_options;
  cups_option_t *options;
  const char *val;
  char *ppdFileName;
  ppd_file_t *ppd = 0;
  ppd_attr_t *attr;
  GooString jobInfoStr;
  GooString docInfoStr;
  GooString pageInfoStr;
  bool colorProfile = false;


  if (argc < 6 || argc > 7) {
    opvpError(-1,"ERROR: %s job-id user title copies options [file]",
      argv[0]);
    return (1);
  }

  if ((ppdFileName = getenv("PPD")) != 0) {
    if ((ppd = ppdOpenFile(ppdFileName)) != 0) {
      /* get attributes from PPD File */
      if ((attr = ppdFindAttr(ppd,"opvpJobInfo",0)) != 0) {
	jobInfoStr.append(attr->value);
      }
      if ((attr = ppdFindAttr(ppd,"opvpDocInfo",0)) != 0) {
	docInfoStr.append(attr->value);
      }
      if ((attr = ppdFindAttr(ppd,"opvpPageInfo",0)) != 0) {
	pageInfoStr.append(attr->value);
      }
      if ((attr = ppdFindAttr(ppd,"pdftoopvpOldLipsDriver",0)) != 0) {
	if (strcasecmp(attr->value,"true") == 0) {
	  oldLipsDriver = true;
	} else {
	  oldLipsDriver = false;
	}
      }
      if ((attr = ppdFindAttr(ppd,"pdftoopvpHPDriver",0)) != 0) {
	if (strcasecmp(attr->value,"true") == 0) {
	  HPDriver = true;
	} else {
	  HPDriver = false;
	}
      }
      if ((attr = ppdFindAttr(ppd,"pdftoopvpNECDriver",0)) != 0) {
	if (strcasecmp(attr->value,"true") == 0) {
	  NECDriver = true;
	} else {
	  NECDriver = false;
	}
      }
      if ((attr = ppdFindAttr(ppd,"pdftoopvpClipPathNotSaved",0)) != 0) {
	if (strcasecmp(attr->value,"true") == 0) {
	  clipPathNotSaved = true;
	} else {
	  clipPathNotSaved = false;
	}
      }
      if ((attr = ppdFindAttr(ppd,"pdftoopvpShearImage",0)) != 0) {
	if (strcasecmp(attr->value,"true") == 0) {
	  noShearImage = false;
	} else {
	  noShearImage = true;
	}
      }
      if ((attr = ppdFindAttr(ppd,"pdftoopvpLineStyle",0)) != 0) {
	if (strcasecmp(attr->value,"true") == 0) {
	  noLineStyle = false;
	} else {
	  noLineStyle = true;
	}
      }
      if ((attr = ppdFindAttr(ppd,"pdftoopvpImageMask",0)) != 0) {
	if (strcasecmp(attr->value,"true") == 0) {
	  noImageMask = false;
	} else {
	  noImageMask = true;
	}
      }
      if ((attr = ppdFindAttr(ppd,"pdftoopvpClipPath",0)) != 0) {
	if (strcasecmp(attr->value,"true") == 0) {
	  noClipPath = false;
	} else {
	  noClipPath = true;
	}
      }
      if ((attr = ppdFindAttr(ppd,"pdftoopvpMiterLimit",0)) != 0) {
	if (strcasecmp(attr->value,"true") == 0) {
	  noMiterLimit = false;
	} else {
	  noMiterLimit = true;
	}
      }
      if ((attr = ppdFindAttr(ppd,"pdftoopvpIgnoreMiterLimit",0)) != 0) {
	if (strcasecmp(attr->value,"true") == 0) {
	  ignoreMiterLimit = true;
	} else {
	  ignoreMiterLimit = false;
	}
      }
      if ((attr = ppdFindAttr(ppd,"pdftoopvpBitmapCharThreshold",0)) != 0) {
	strncpy(bitmapCharThreshold,attr->value,
	  sizeof(bitmapCharThreshold)-1);
      }
      if ((attr = ppdFindAttr(ppd,"pdftoopvpMaxClipPathLength",0)) != 0) {
	strncpy(maxClipPathLength,attr->value,
	  sizeof(maxClipPathLength)-1);
      }
      if ((attr = ppdFindAttr(ppd,"pdftoopvpMaxFillPathLength",0)) != 0) {
	strncpy(maxFillPathLength,attr->value,
	  sizeof(maxFillPathLength)-1);
      }
      if ((attr = ppdFindAttr(ppd,"pdftoopvpBitmapChar",0)) != 0) {
	if (strcasecmp(attr->value,"true") == 0) {
	  noBitmapChar = false;
	} else {
	  noBitmapChar = true;
	}
      }
      if ((attr = ppdFindAttr(ppd,"opvpDriver",0)) != 0) {
          strncpy(printerDriver,attr->value,sizeof(printerDriver)-1);
	  printerDriver[sizeof(printerDriver)-1] = '\0';
      }
      if ((attr = ppdFindAttr(ppd,"opvpModel",0)) != 0) {
          strncpy(printerModel,attr->value,sizeof(printerModel)-1);
	  printerModel[sizeof(printerModel)-1] = '\0';
      }
      ppdMarkDefaults(ppd);
    }
  }
  /* get attributes and options from command line option */
  num_options = cupsParseOptions(argv[5],0,&options);
  cupsMarkOptions(ppd,num_options,options);

  for (i = 0;i < num_options;i++) {
    if (strcasecmp(options[i].name,"Resolution") == 0) {
      resolution = atoi(options[i].value);
    } else if (strcasecmp(options[i].name,"pdftoopvpOldLipsDriver") == 0) {
      oldLipsDriver = true;
    } else if (strcasecmp(options[i].name,"pdftoopvpHPDriver") == 0) {
      HPDriver = true;
    } else if (strcasecmp(options[i].name,"pdftoopvpNECDriver") == 0) {
      NECDriver = true;
    } else if (strcasecmp(options[i].name,"pdftoopvpClipPathNotSaved") == 0) {
      clipPathNotSaved = true;
    } else if (strcasecmp(options[i].name,"pdftoopvpShearImage") == 0) {
      if (strcasecmp(options[i].value,"false") == 0) {
	noShearImage = true;
      }
    } else if (strcasecmp(options[i].name,"pdftoopvpLineStyle") == 0) {
      if (strcasecmp(options[i].value,"false") == 0) {
	noLineStyle = true;
      }
    } else if (strcasecmp(options[i].name,"pdftoopvpImageMask") == 0) {
      if (strcasecmp(options[i].value,"false") == 0) {
	noImageMask = true;
      }
    } else if (strcasecmp(options[i].name,"pdftoopvpClipPath") == 0) {
      if (strcasecmp(options[i].value,"false") == 0) {
	noClipPath = true;
      }
    } else if (strcasecmp(options[i].name,"pdftoopvpMiterLimit") == 0) {
      if (strcasecmp(options[i].value,"false") == 0) {
	noMiterLimit = true;
      }
    } else if (strcasecmp(options[i].name,"pdftoopvpIgnoreMiterLimit") == 0) {
      if (strcasecmp(options[i].value,"true") == 0) {
	ignoreMiterLimit = true;
      }
    }
     else if (strcasecmp(options[i].name,"pdftoopvpBitmapChar") == 0) {
      if (strcasecmp(options[i].value,"false") == 0) {
	noBitmapChar = true;
      }
    } else if (strcasecmp(options[i].name,"pdftoopvpBitmapCharThreshold") == 0) {
      strncpy(bitmapCharThreshold,options[i].value,
        sizeof(bitmapCharThreshold)-1);
    } else if (strcasecmp(options[i].name,"pdftoopvpMaxClipPathLength") == 0) {
      strncpy(maxClipPathLength,options[i].value,
        sizeof(maxClipPathLength)-1);
    } else if (strcasecmp(options[i].name,"pdftoopvpMaxFillPathLength") == 0) {
      strncpy(maxFillPathLength,options[i].value,
        sizeof(maxFillPathLength)-1);
    } else if (strcasecmp(options[i].name,"opvpDriver") == 0) {
      strncpy(printerDriver,options[i].value,sizeof(printerDriver)-1);
      printerDriver[sizeof(printerDriver)-1] = '\0';
    } else if (strcasecmp(options[i].name,"opvpModel") == 0) {
      strncpy(printerModel,options[i].value,sizeof(printerModel)-1);
      printerModel[sizeof(printerModel)-1] = '\0';
    } else if (strcasecmp(options[i].name,"opvpJobInfo") == 0) {
      /* do nothing here */;
    } else if (strcasecmp(options[i].name,"opvpDocInfo") == 0) {
      /* do nothing here */;
    } else if (strcasecmp(options[i].name,"opvpPageInfo") == 0) {
      /* do nothing here */;
    }
  }
  if (ppd != 0) {
    int section;
    ppd_choice_t **choices;
    ppd_size_t *pagesize;

    if ((pagesize = ppdPageSize(ppd,0)) != 0) {
      pageWidth = (int)pagesize->width;
      pageHeight = (int)pagesize->length;
    }
    for (section = (int)PPD_ORDER_ANY;
      section <= (int)PPD_ORDER_PROLOG;section++) {
      int n;

      n = ppdCollect(ppd,(ppd_section_t)section,&choices);
      for (i = 0;i < n;i++) {

	if (strcasecmp(((ppd_option_t *)(choices[i]->option))->keyword,
	   "Resolution") == 0) {
	  resolution = atoi(choices[i]->choice);
	}
      }
      if (choices != 0) free(choices);
    }

#if POPPLER_VERSION_MAJOR == 0 && POPPLER_VERSION_MINOR <= 71
    strncpy(jobInfo,jobInfoStr.getCString(),sizeof(jobInfo)-1);
    jobInfo[sizeof(jobInfo)-1] = '\0';
    strncpy(docInfo,docInfoStr.getCString(),sizeof(docInfo)-1);
    docInfo[sizeof(docInfo)-1] = '\0';
    strncpy(pageInfo,pageInfoStr.getCString(),sizeof(pageInfo)-1);
    pageInfo[sizeof(pageInfo)-1] = '\0';
#else
    strncpy(jobInfo,jobInfoStr.c_str(),sizeof(jobInfo)-1);
    jobInfo[sizeof(jobInfo)-1] = '\0';
    strncpy(docInfo,docInfoStr.c_str(),sizeof(docInfo)-1);
    docInfo[sizeof(docInfo)-1] = '\0';
    strncpy(pageInfo,pageInfoStr.c_str(),sizeof(pageInfo)-1);
    pageInfo[sizeof(pageInfo)-1] = '\0';
#endif

    colorProfile = getColorProfilePath(ppd,&colorProfilePath);

    ppdClose(ppd);
  }
  if ((val = cupsGetOption("opvpJobInfo",num_options, options)) != 0) {
    /* override ppd value */
    strncpy(jobInfo,val,sizeof(jobInfo)-1);
    jobInfo[sizeof(jobInfo)-1] = '\0';
  }
  if ((val = cupsGetOption("opvpDocInfo",num_options, options)) != 0) {
    /* override ppd value */
    strncpy(docInfo,val,sizeof(docInfo)-1);
    docInfo[sizeof(docInfo)-1] = '\0';
  }
  if ((val = cupsGetOption("opvpPageInfo",num_options, options)) != 0) {
    /* override ppd value */
    strncpy(pageInfo,val,sizeof(pageInfo)-1);
    pageInfo[sizeof(pageInfo)-1] = '\0';
  }

  cupsFreeOptions(num_options,options);
#if 0
  /* for debug parameters */
  fprintf(stderr,"WARNING:resolution=%d\n",resolution);
  fprintf(stderr,"WARNING:sliceHeight=%d\n",sliceHeight);
  fprintf(stderr,"WARNING:oldLipsDriver=%d\n",oldLipsDriver);
  fprintf(stderr,"WARNING:HPDriver=%d\n",HPDriver);
  fprintf(stderr,"WARNING:NECDriver=%d\n",NECDriver);
  fprintf(stderr,"WARNING:clipPathNotSaved=%d\n",clipPathNotSaved);
  fprintf(stderr,"WARNING:noShearImage=%d\n",noShearImage);
  fprintf(stderr,"WARNING:noLineStyle=%d\n",noLineStyle);
  fprintf(stderr,"WARNING:noClipPath=%d\n",noClipPath);
  fprintf(stderr,"WARNING:noMiterLimit=%d\n",noMiterLimit);
  fprintf(stderr,"WARNING:printerDriver=%s\n",printerDriver);
  fprintf(stderr,"WARNING:printerModel=%s\n",printerModel);
  fprintf(stderr,"WARNING:jobInfo=%s\n",jobInfo);
  fprintf(stderr,"WARNING:docInfo=%s\n",docInfo);
  fprintf(stderr,"WARNING:pageInfo=%s\n",pageInfo);
  fprintf(stderr,"WARNING:noBitmapChar=%d\n",noBitmapChar);
  fprintf(stderr,"WARNING:bitmapCharThreshold=%s\n",bitmapCharThreshold);
  fprintf(stderr,"WARNING:maxClipPathLength=%s\n",maxClipPathLength);
  fprintf(stderr,"WARNING:maxFillPathLength=%s\n",maxFillPathLength);
exit(0);
#endif

  if (oldLipsDriver) {
    optionKeys[nOptions] = "OPVP_OLDLIPSDRIVER";
    optionVals[nOptions] = "1";
    nOptions++;
    clipPathNotSaved = true;
    noShearImage = true;
  }
  if (HPDriver) {
    noClipPath = true;
    noLineStyle = true;
    noShearImage = true;
  }
  if (NECDriver) {
    noMiterLimit = true;
    strcpy(maxClipPathLength,"6");
    noShearImage = true;
  }
  if (clipPathNotSaved) {
    optionKeys[nOptions] = "OPVP_CLIPPATHNOTSAVED";
    optionVals[nOptions] = "1";
    nOptions++;
  }
  if (noShearImage) {
    optionKeys[nOptions] = "OPVP_NOSHEARIMAGE";
    optionVals[nOptions] = "1";
    nOptions++;
  }
  if (noLineStyle) {
    optionKeys[nOptions] = "OPVP_NOLINESTYLE";
    optionVals[nOptions] = "1";
    nOptions++;
  }
  if (noImageMask) {
    optionKeys[nOptions] = "OPVP_NOIMAGEMASK";
    optionVals[nOptions] = "1";
    nOptions++;
  }
  if (noClipPath) {
    optionKeys[nOptions] = "OPVP_NOCLIPPATH";
    optionVals[nOptions] = "1";
    nOptions++;
  }
  if (noMiterLimit) {
    optionKeys[nOptions] = "OPVP_NOMITERLIMIT";
    optionVals[nOptions] = "1";
    nOptions++;
  }
  if (noBitmapChar) {
    optionKeys[nOptions] = "OPVP_NOBITMAPCHAR";
    optionVals[nOptions] = "1";
    nOptions++;
  }
  if (ignoreMiterLimit) {
    optionKeys[nOptions] = "OPVP_IGNOREMITERLIMIT";
    optionVals[nOptions] = "1";
    nOptions++;
  }
  optionKeys[nOptions] = "OPVP_BITMAPCHARTHRESHOLD";
  optionVals[nOptions] = bitmapCharThreshold;
  nOptions++;
  optionKeys[nOptions] = "OPVP_MAXCLIPPATHLENGTH";
  optionVals[nOptions] = maxClipPathLength;
  nOptions++;
  optionKeys[nOptions] = "OPVP_MAXFILLPATHLENGTH";
  optionVals[nOptions] = maxFillPathLength;
  nOptions++;
  if (hResolution == 0) hResolution = resolution;
  if (hResolution == 0) hResolution = resolution;
  if (vResolution == 0) vResolution = resolution;
  if (strcasecmp(outputOrderStr,"reverse") == 0) {
    reverse = true;
  }

  if (argc > 6) {
    fileName.append(argv[6]);
  } else {
    fileName.append("-");
  }

  // read config file
  globalParams = new GlobalParams();
  if (enableFreeTypeStr[0]) {
    if (!globalParams->setEnableFreeType(enableFreeTypeStr)) {
      opvpError(-1,"Bad '-freetype' value on command line");
      ok = false;
    }
  }
#if POPPLER_VERSION_MAJOR == 0 && POPPLER_VERSION_MINOR <= 30
  globalParams->setAntialias("no");
#endif
  if (quiet) {
    globalParams->setErrQuiet(quiet);
  }
  if (!ok) {
    exitCode = 2;
    goto err0;
  }

  if (fileName.cmp("-") == 0) {
    /* stdin */
    char *s;
    GooString name;
    int fd;
    char buf[4096];
    int n;

    /* create a tmp file */
    if ((s = getenv("TMPDIR")) != 0) {
      name.append(s);
    } else {
      name.append("/tmp");
    }
    name.append("/XXXXXX");
#if POPPLER_VERSION_MAJOR == 0 && POPPLER_VERSION_MINOR <= 71
    fd = mkstemp(name.getCString());
#else
    std::string t = name.toStr();
    fd = mkstemp(&t[0]);
    name = GooString(std::move(t));
#endif
    if (fd < 0) {
      opvpError(-1,"Can't create temporary file");
      exitCode = 2;
      goto err0;
    }

    /* check JCL */
    while (fgets(buf,sizeof(buf)-1,stdin) != NULL
        && strncmp(buf,"%PDF",4) != 0) {
      if (strncmp(buf,"pdftoopvp jobInfo:",18) == 0) {
	/* JCL jobInfo exists, override jobInfo */
	strncpy(jobInfo,buf+18,sizeof(jobInfo)-1);
	for (i = sizeof(jobInfo)-2;i >= 0
	  && (jobInfo[i] == 0 || jobInfo[i] == '\n' || jobInfo[i] == ';')
	  ;i--);
	jobInfo[i+1] = 0;
      }
    }
    if (strncmp(buf,"%PDF",4) != 0) {
      opvpError(-1,"Can't find PDF header");
      exitCode = 2;
      goto err0;
    }
    /* copy PDF header */
    n = strlen(buf);
    if (write(fd,buf,n) != n) {
      opvpError(-1,"Can't copy stdin to temporary file");
      close(fd);
      exitCode = 2;
      goto err0;
    }
    /* copy rest stdin to the tmp file */
    while ((n = fread(buf,1,sizeof(buf),stdin)) > 0) {
      if (write(fd,buf,n) != n) {
	opvpError(-1,"Can't copy stdin to temporary file");
	close(fd);
	exitCode = 2;
	goto err0;
      }
    }
    close(fd);
    doc = new PDFDoc(&name);
    /* remove name */
#if POPPLER_VERSION_MAJOR == 0 && POPPLER_VERSION_MINOR <= 71
    unlink(name.getCString());
#else
    unlink(name.c_str());
#endif
  } else {
    /* no jcl check */
    doc = new PDFDoc(fileName.copy());
  }
  if (!doc->isOk()) {
    opvpError(-1," Parsing PDF failed: error code %d",
      doc->getErrorCode());
    exitCode = 2;
    goto err05;
  }

  if (doc->isEncrypted() && !doc->okToPrint()) {
    opvpError(-1,"Print Permission Denied");
    exitCode = 2;
    goto err05;
  }

  /* paperColor is white */
  paperColor[0] = 255;
  paperColor[1] = 255;
  paperColor[2] = 255;
#ifdef USE_CMS
  /* set color profile file name */
  GfxColorSpace::setDisplayProfileName(&colorProfilePath);
#endif
  opvpOut = new OPVPOutputDev();
  if (opvpOut->init(mono ? splashModeMono1 :
				    gray ? splashModeMono8 :
				             splashModeRGB8,
				  colorProfile,
				  false, paperColor,
                                 printerDriver,1,printerModel,
				 nOptions,optionKeys,optionVals) < 0) {
      opvpError(-1,"OPVPOutputDev Initialize fail");
      exitCode = 2;
      goto err1;
  }

  opvpOut->startDoc(doc->getXRef());

#if 0
fprintf(stderr,"JobInfo=%s\n",jobInfo);
#endif
  if (opvpOut->OPVPStartJob(jobInfo) < 0) {
      opvpError(-1,"Start job failed");
      exitCode = 2;
      goto err1;
  }
  if (opvpOut->OPVPStartDoc(docInfo) < 0) {
      opvpError(-1,"Start Document failed");
      exitCode = 2;
      goto err2;
  }
  numPages = doc->getNumPages();
  for (pg = 1; pg <= numPages; ++pg) {
    if ((exitCode = outOnePage(doc,opvpOut,pg)) != 0) break;
  }
  if (opvpOut->OPVPEndDoc() < 0) {
      opvpError(-1,"End Document failed");
      exitCode = 2;
  }
err2:
  if (opvpOut->OPVPEndJob() < 0) {
      opvpError(-1,"End job failed");
      exitCode = 2;
  }

  // clean up
 err1:
  delete opvpOut;
 err05:
  delete doc;
 err0:
  delete globalParams;

#if POPPLER_VERSION_MAJOR == 0 && POPPLER_VERSION_MINOR < 69
  // check for memory leaks
  Object::memCheck(stderr);
  gMemReport(stderr);
#endif

}
/* muntrace(); */
  return exitCode;
}

/* for memory debug */
/* For compatibility with g++ >= 4.7 compilers _GLIBCXX_THROW
 *  should be used as a guard, otherwise use traditional definition */
#ifndef _GLIBCXX_THROW
#define _GLIBCXX_THROW throw
#endif

void * operator new(size_t size) _GLIBCXX_THROW (std::bad_alloc)
{
    void *p = malloc(size);
    return p;
}

void operator delete(void *p) throw ()
{
    free(p);
}
