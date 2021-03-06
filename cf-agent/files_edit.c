/* 

   Copyright (C) Cfengine AS

   This file is part of Cfengine 3 - written and maintained by Cfengine AS.
 
   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; version 3.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 
  You should have received a copy of the GNU General Public License  
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA

  To the extent this program is licensed as part of the Enterprise
  versions of Cfengine, the applicable Commerical Open Source License
  (COSL) may apply to this file if you as a licensee so wish it. See
  included file COSL.txt.
*/

#include "files_edit.h"

#include "env_context.h"
#include "files_names.h"
#include "files_interfaces.h"
#include "files_operators.h"
#include "files_lib.h"
#include "item_lib.h"
#include "cfstream.h"
#include "logging.h"
#include "policy.h"

/*****************************************************************************/

EditContext *NewEditContext(EvalContext *ctx, char *filename, Attributes a, const Promise *pp)
{
    EditContext *ec;

    if (!IsAbsoluteFileName(filename))
    {
        CfOut(OUTPUT_LEVEL_ERROR, "", "Relative file name %s was marked for editing but has no invariant meaning\n", filename);
        return NULL;
    }

    ec = xcalloc(1, sizeof(EditContext));

    ec->filename = filename;
    ec->empty_first = a.edits.empty_before_use;

    if (a.haveeditline)
    {
        if (!LoadFileAsItemList(ctx, &(ec->file_start), filename, a, pp))
        {
        free(ec);
        return NULL;
        }
    }

    if (a.haveeditxml)
    {
#ifdef HAVE_LIBXML2
        if (!LoadFileAsXmlDoc(ctx, &(ec->xmldoc), filename, a, pp))
        {
            free(ec);
            return NULL;
        }
#else
        cfPS(ctx, OUTPUT_LEVEL_ERROR, CF_FAIL, "", pp, a, " !! Cannot edit XML files without LIBXML2\n");
        free(ec);
        return NULL;
#endif
    }

    if (a.edits.empty_before_use)
    {
        CfOut(OUTPUT_LEVEL_VERBOSE, "", " -> Build file model from a blank slate (emptying)\n");
        DeleteItemList(ec->file_start);
        ec->file_start = NULL;
    }

    EDIT_MODEL = true;
    return ec;
}

/*****************************************************************************/

void FinishEditContext(EvalContext *ctx, EditContext *ec, Attributes a, Promise *pp)
{
    Item *ip;

    EDIT_MODEL = false;

    if (DONTDO || (a.transaction.action == cfa_warn))
    {
        if (ec && (!CompareToFile(ctx, ec->file_start, ec->filename, a, pp)) && (ec->num_edits > 0))
        {
            cfPS(ctx, OUTPUT_LEVEL_ERROR, CF_WARN, "", pp, a, " -> Should edit file %s but only a warning promised", ec->filename);
        }
        return;
    }
    else if (ec && (ec->num_edits > 0))
    {
        if (a.haveeditline)
        {
            if (CompareToFile(ctx, ec->file_start, ec->filename, a, pp))
            {
                if (ec)
                {
                    cfPS(ctx, OUTPUT_LEVEL_VERBOSE, CF_NOP, "", pp, a, " -> No edit changes to file %s need saving", ec->filename);
                }
            }
            else
            {
                SaveItemListAsFile(ctx, ec->file_start, ec->filename, a, pp);
            }
        }

        if (a.haveeditxml)
        {
#ifdef HAVE_LIBXML2
            if (XmlCompareToFile(ctx, ec->xmldoc, ec->filename, a, pp))
            {
                if (ec)
                {
                    cfPS(ctx, OUTPUT_LEVEL_VERBOSE, CF_NOP, "", pp, a, " -> No edit changes to xml file %s need saving", ec->filename);
                }
            }
            else
            {
                SaveXmlDocAsFile(ctx, ec->xmldoc, ec->filename, a, pp);
            }
            xmlFreeDoc(ec->xmldoc);
#else
            cfPS(ctx, OUTPUT_LEVEL_ERROR, CF_FAIL, "", pp, a, " !! Cannot edit XML files without LIBXML2\n");
#endif
        }
    }
    else
    {
        if (ec)
        {
            cfPS(ctx, OUTPUT_LEVEL_VERBOSE, CF_NOP, "", pp, a, " -> No edit changes to file %s need saving", ec->filename);
        }
    }

    if (ec != NULL)
    {
        for (ip = ec->file_classes; ip != NULL; ip = ip->next)
        {
            EvalContextHeapAddSoft(ctx, ip->name, PromiseGetNamespace(pp));
        }

        DeleteItemList(ec->file_classes);
        DeleteItemList(ec->file_start);
    }
}

/*********************************************************************/
/* Level                                                             */
/*********************************************************************/

/***************************************************************************/

#ifdef HAVE_LIBXML2
int LoadFileAsXmlDoc(EvalContext *ctx, xmlDocPtr *doc, const char *file, Attributes a, const Promise *pp)
{
    struct stat statbuf;

    if (cfstat(file, &statbuf) == -1)
    {
        cfPS(ctx, OUTPUT_LEVEL_ERROR, CF_FAIL, "stat", pp, a, " ** Information: the proposed file \"%s\" could not be loaded", file);
        return false;
    }

    if (a.edits.maxfilesize != 0 && statbuf.st_size > a.edits.maxfilesize)
    {
        CfOut(OUTPUT_LEVEL_INFORM, "", " !! File %s is bigger than the limit edit.max_file_size = %jd > %d bytes\n", file,
              (intmax_t) statbuf.st_size, a.edits.maxfilesize);
        return false;
    }

    if (!S_ISREG(statbuf.st_mode))
    {
        cfPS(ctx, OUTPUT_LEVEL_INFORM, CF_INTERPT, "", pp, a, "%s is not a plain file\n", file);
        return false;
    }

    if (statbuf.st_size == 0)
    {
        if ((*doc = xmlNewDoc(BAD_CAST "1.0")) == NULL)
        {
            cfPS(ctx, OUTPUT_LEVEL_INFORM, CF_INTERPT, "xmlParseFile", pp, a, "Document %s not parsed successfully\n", file);
            return false;
        }
    }
    else if ((*doc = xmlParseFile(file)) == NULL)
    {
        cfPS(ctx, OUTPUT_LEVEL_INFORM, CF_INTERPT, "xmlParseFile", pp, a, "Document %s not parsed successfully\n", file);
        return false;
    }

    return true;
}
#endif


/*********************************************************************/

#ifdef HAVE_LIBXML2
bool SaveXmlCallback(EvalContext *ctx, const char *dest_filename, const char *orig_filename, void *param, Attributes a, Promise *pp)
{
    xmlDocPtr doc = param;

    //saving xml to file
    if (xmlSaveFile(dest_filename, doc) == -1)
    {
        cfPS(ctx, OUTPUT_LEVEL_ERROR, CF_FAIL, "xmlSaveFile", pp, a, "Failed to write xml document to file %s after editing\n", dest_filename);
        return false;
    }

    cfPS(ctx, OUTPUT_LEVEL_INFORM, CF_CHG, "", pp, a, " -> Edited xml file %s \n", orig_filename);
    return true;
}
#endif

/*********************************************************************/

#ifdef HAVE_LIBXML2
int SaveXmlDocAsFile(EvalContext *ctx, xmlDocPtr doc, const char *file, Attributes a, Promise *pp)
{
    return SaveAsFile(ctx, &SaveXmlCallback, doc, file, a, pp);
}
#endif
