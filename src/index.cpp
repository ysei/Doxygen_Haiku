/******************************************************************************
 *
 * 
 *
 * Copyright (C) 1997-2011 by Dimitri van Heesch.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation under the terms of the GNU General Public License is hereby 
 * granted. No representations are made about the suitability of this software 
 * for any purpose. It is provided "as is" without express or implied warranty.
 * See the GNU General Public License for more details.
 *
 * Documents produced by Doxygen are derivative works derived from the
 * input used in their production; they are not affected by this license.
 *
 */

/** @file
 *  @brief This file contains functions for the various index pages.
 */

#include <stdlib.h>

#include <qtextstream.h>
#include <qdatetime.h>
#include <qdir.h>
#include <qregexp.h>

#include "message.h"
#include "index.h"
#include "doxygen.h"
#include "config.h"
#include "filedef.h"
#include "outputlist.h"
#include "util.h"
#include "groupdef.h"
#include "language.h"
#include "htmlgen.h"
#include "htmlhelp.h"
#include "ftvhelp.h"
#include "dot.h"
#include "pagedef.h"
#include "dirdef.h"
#include "vhdldocgen.h"
#include "layout.h"

#define MAX_ITEMS_BEFORE_MULTIPAGE_INDEX 200
#define MAX_ITEMS_BEFORE_QUICK_INDEX 30

static const char search_script[]=
#include "search_js.h"
;

//static const char navindex_script[]=
//#include "navindex_js.h"
//;

int annotatedClasses;
int annotatedClassesPrinted;
int hierarchyClasses;
int documentedFiles;
int documentedGroups;
int documentedNamespaces;
int indexedPages;
int documentedClassMembers[CMHL_Total];
int documentedFileMembers[FMHL_Total];
int documentedNamespaceMembers[NMHL_Total];
int documentedHtmlFiles;
int documentedPages;
int documentedDirs;

int countClassHierarchy();
int countClassMembers(int filter=CMHL_All);
int countFileMembers(int filter=FMHL_All);
void countFiles(int &htmlFiles,int &files);
int countGroups();
int countDirs();
int countNamespaces();
int countAnnotatedClasses(int *cp);
int countNamespaceMembers(int filter=NMHL_All);
int countIncludeFiles();
void countRelatedPages(int &docPages,int &indexPages);

void countDataStructures()
{
  annotatedClasses           = countAnnotatedClasses(&annotatedClassesPrinted); // "classes" + "annotated"
  hierarchyClasses           = countClassHierarchy();   // "hierarchy"
  countFiles(documentedHtmlFiles,documentedFiles);      // "files"
  countRelatedPages(documentedPages,indexedPages);      // "pages"
  documentedGroups           = countGroups();           // "modules"
  documentedNamespaces       = countNamespaces();       // "namespaces"
  documentedDirs             = countDirs();             // "dirs"
  // "globals"
  // "namespacemembers"
  // "functions"
}

static void startIndexHierarchy(OutputList &ol,int level)
{
  ol.pushGeneratorState();
  ol.disable(OutputGenerator::Man);
  ol.disable(OutputGenerator::Html);
  if (level<6) ol.startIndexList();
  ol.enableAll();
  ol.disable(OutputGenerator::Latex);
  ol.disable(OutputGenerator::RTF);
  ol.startItemList();
  ol.popGeneratorState();
}

static void endIndexHierarchy(OutputList &ol,int level)
{
  ol.pushGeneratorState();
  ol.disable(OutputGenerator::Man);
  ol.disable(OutputGenerator::Html);
  if (level<6) ol.endIndexList();
  ol.enableAll();
  ol.disable(OutputGenerator::Latex);
  ol.disable(OutputGenerator::RTF);
  ol.endItemList();
  ol.popGeneratorState();
}

//----------------------------------------------------------------------------

class MemberIndexList : public QList<MemberDef>
{
  public:
    MemberIndexList() : QList<MemberDef>() {}
    ~MemberIndexList() {}
    int compareItems(GCI item1, GCI item2)
    {
      MemberDef *md1=(MemberDef *)item1;
      MemberDef *md2=(MemberDef *)item2;
      return stricmp(md1->name(),md2->name());
    }
};

#define MEMBER_INDEX_ENTRIES 256

static MemberIndexList g_memberIndexLetterUsed[CMHL_Total][MEMBER_INDEX_ENTRIES];
static MemberIndexList g_fileIndexLetterUsed[FMHL_Total][MEMBER_INDEX_ENTRIES];
static MemberIndexList g_namespaceIndexLetterUsed[NMHL_Total][MEMBER_INDEX_ENTRIES];

static bool g_classIndexLetterUsed[CHL_Total][256];

const int maxItemsBeforeQuickIndex = MAX_ITEMS_BEFORE_QUICK_INDEX;

//----------------------------------------------------------------------------

// strips w from s iff s starts with w
bool stripWord(QCString &s,QCString w)
{
  bool success=FALSE;
  if (s.left(w.length())==w) 
  {
    success=TRUE;
    s=s.right(s.length()-w.length());
  }
  return success;
}

//----------------------------------------------------------------------------
// some quasi intelligent brief description abbreviator :^)
QCString abbreviate(const char *s,const char *name)
{
  QCString scopelessName=name;
  int i=scopelessName.findRev("::");
  if (i!=-1) scopelessName=scopelessName.mid(i+2);
  QCString result=s;
  result=result.stripWhiteSpace();
  // strip trailing .
  if (!result.isEmpty() && result.at(result.length()-1)=='.') 
    result=result.left(result.length()-1);

  // strip any predefined prefix
  QStrList &briefDescAbbrev = Config_getList("ABBREVIATE_BRIEF");
  const char *p = briefDescAbbrev.first();
  while (p)
  {
    QCString s = p;
    s.replace(QRegExp("\\$name"), scopelessName);  // replace $name with entity name
    s += " ";
    stripWord(result,s);
    p = briefDescAbbrev.next();
  }

  // capitalize first word
  if (!result.isEmpty())
  {
    int c=result[0];
    if (c>='a' && c<='z') c+='A'-'a';
    result[0]=c;
  }
  return result;
}

//----------------------------------------------------------------------------

static void startQuickIndexList(OutputList &ol,bool letterTabs=FALSE)
{
  bool fancyTabs = TRUE;
  if (fancyTabs)
  {
    if (letterTabs)
    {
      ol.writeString("  <div id=\"navrow4\" class=\"tabs3\">\n"); 
    }
    else
    {
      ol.writeString("  <div id=\"navrow3\" class=\"tabs2\">\n"); 
    }
    ol.writeString("    <ul class=\"tablist\">\n"); 
  }
  else
  {
    ol.writeString("  <div class=\"qindex\">"); 
  }
}

static void endQuickIndexList(OutputList &ol)
{
  bool fancyTabs = TRUE;
  if (fancyTabs)
  {
    ol.writeString("    </ul>\n");
  }
  ol.writeString("  </div>\n");
}

static void startQuickIndexItem(OutputList &ol,const char *l,
                                bool hl,bool compact,bool &first)
{
  bool fancyTabs = TRUE;
  if (!first && compact && !fancyTabs) ol.writeString(" | ");
  first=FALSE;
  if (fancyTabs)
  {
    ol.writeString("      <li"); 
    if (hl) ol.writeString(" class=\"current\"");
    ol.writeString("><a ");
  }
  else
  {
    if (!compact) ol.writeString("<li>");
    if (hl && compact)
    {
      ol.writeString("<a class=\"qindexHL\" ");
    }
    else
    {
      ol.writeString("<a class=\"qindex\" ");
    }
  }
  ol.writeString("href=\""); 
  ol.writeString(l);
  ol.writeString("\">");
  if (fancyTabs)
  {
    ol.writeString("<span>");
  }
}

static void endQuickIndexItem(OutputList &ol)
{
  bool fancyTabs=TRUE;
  if (fancyTabs) ol.writeString("</span>");
  ol.writeString("</a>");
  if (fancyTabs) ol.writeString("</li>\n");
}


static QCString fixSpaces(const QCString &s)
{
  return substitute(s," ","&#160;");
}


void startTitle(OutputList &ol,const char *fileName,Definition *def)
{
  ol.startHeaderSection();
  if (def) def->writeSummaryLinks(ol);
  ol.startTitleHead(fileName);
  ol.pushGeneratorState();
  ol.disable(OutputGenerator::Man);
}

void endTitle(OutputList &ol,const char *fileName,const char *name)
{
  ol.popGeneratorState();
  ol.endTitleHead(fileName,name);
  ol.endHeaderSection();
}

void startFile(OutputList &ol,const char *name,const char *manName,
               const char *title,HighlightedItem hli,bool additionalIndices,
               const char *altSidebarName)
{
  static bool disableIndex     = Config_getBool("DISABLE_INDEX");
  static bool generateTreeView = Config_getBool("GENERATE_TREEVIEW");
  ol.startFile(name,manName,title);
  ol.startQuickIndices();
  if (!disableIndex)
  {
    ol.writeQuickLinks(TRUE,hli);
  }
  if (!additionalIndices)
  {
    ol.endQuickIndices();
  }
  if (generateTreeView)
  {
    ol.writeSplitBar(altSidebarName ? altSidebarName : name);
  }
}

void endFile(OutputList &ol,bool skipNavIndex)
{
  static bool generateTreeView = Config_getBool("GENERATE_TREEVIEW");
  ol.pushGeneratorState();
  ol.disableAllBut(OutputGenerator::Html);
  if (!skipNavIndex)
  {
    ol.endContents();
    if (generateTreeView)
    {
      ol.writeString("</div>\n");
      ol.writeString("  <div id=\"nav-path\" class=\"navpath\">\n");
      ol.writeString("    <ul>\n");
    }
  }
  ol.writeFooter(); // write the footer
  ol.popGeneratorState();
  ol.endFile();
}

//----------------------------------------------------------------------------

static bool classHasVisibleChildren(ClassDef *cd)
{
 bool vhdl=Config_getBool("OPTIMIZE_OUTPUT_VHDL");

  BaseClassList *bcl;

  if (vhdl) // reverse baseClass/subClass relation
  {
    if (cd->baseClasses()==0) return FALSE;
    bcl=cd->baseClasses();
  }
  else 
  {
    if (cd->subClasses()==0) return FALSE;
    bcl=cd->subClasses();
  }

  BaseClassListIterator bcli(*bcl);
  for ( ; bcli.current() ; ++bcli)
  {
    if (bcli.current()->classDef->isVisibleInHierarchy())
    {
      return TRUE;
    }
  }
  return FALSE;
}

void writeClassTree(OutputList &ol,BaseClassList *bcl,bool hideSuper,int level,FTVHelp* ftv)
{
  static bool vhdl=Config_getBool("OPTIMIZE_OUTPUT_VHDL");

  if (bcl==0) return;
  BaseClassListIterator bcli(*bcl);
  bool started=FALSE;
  for ( ; bcli.current() ; ++bcli)
  {
    ClassDef *cd=bcli.current()->classDef;
    bool b;
    if (vhdl)
    {
      b=hasVisibleRoot(cd->subClasses());
    }
    else
    {
      b=hasVisibleRoot(cd->baseClasses());
    }

    if (cd->isVisibleInHierarchy() && b) // hasVisibleRoot(cd->baseClasses()))
    {
      if (!started)
      {
        startIndexHierarchy(ol,level);
        Doxygen::indexList.incContentsDepth();
        if (ftv)
          ftv->incContentsDepth();
        started=TRUE;
      }
      ol.startIndexListItem();
      //printf("Passed...\n");
      bool hasChildren = !cd->visited && !hideSuper && classHasVisibleChildren(cd);
      //printf("tree4: Has children %s: %d\n",cd->name().data(),hasChildren);
      if (cd->isLinkable())
      {
        //printf("Writing class %s\n",cd->displayName().data());
        ol.startIndexItem(cd->getReference(),cd->getOutputFileBase());
        ol.parseText(cd->displayName());
        ol.endIndexItem(cd->getReference(),cd->getOutputFileBase());
        if (cd->isReference()) 
        { 
          ol.startTypewriter(); 
          ol.docify(" [external]");
          ol.endTypewriter();
        }
        Doxygen::indexList.addContentsItem(hasChildren,cd->displayName(),cd->getReference(),cd->getOutputFileBase(),cd->anchor());
        if (ftv)
          ftv->addContentsItem(hasChildren,cd->displayName(),cd->getReference(),cd->getOutputFileBase(),cd->anchor());
      }
      else
      {
        ol.startIndexItem(0,0);
        ol.parseText(cd->name());
        ol.endIndexItem(0,0);
        Doxygen::indexList.addContentsItem(hasChildren,cd->displayName(),0,0,0);
        if (ftv)
          ftv->addContentsItem(hasChildren,cd->displayName(),0,0,0);
      }
      if (hasChildren)
      {
        //printf("Class %s at %p visited=%d\n",cd->name().data(),cd,cd->visited);
        bool wasVisited=cd->visited;
        cd->visited=TRUE;
        if (vhdl)	
        {
          writeClassTree(ol,cd->baseClasses(),wasVisited,level+1,ftv);
        }
        else       
        {
          writeClassTree(ol,cd->subClasses(),wasVisited,level+1,ftv);
        }
      }
      ol.endIndexListItem();
    }
  }
  if (started) 
  {
    endIndexHierarchy(ol,level);
    Doxygen::indexList.decContentsDepth();
    if (ftv)
      ftv->decContentsDepth();
  }
}


//----------------------------------------------------------------------------
/*! Generates HTML Help tree of classes */

void writeClassTree(BaseClassList *cl,int level)
{
  if (cl==0) return;
  BaseClassListIterator cli(*cl);
  bool started=FALSE;
  for ( ; cli.current() ; ++cli)
  {
    ClassDef *cd=cli.current()->classDef;
    if (cd->isVisibleInHierarchy() && hasVisibleRoot(cd->baseClasses()))
    //if (cd->isVisibleInHierarchy() && !cd->visited)
    {
      if (!started)
      {
        Doxygen::indexList.incContentsDepth();
        started=TRUE;
      }
      bool hasChildren = !cd->visited && classHasVisibleChildren(cd);
      //printf("tree2: Has children %s: %d\n",cd->name().data(),hasChildren);
      if (cd->isLinkable())
      {
        Doxygen::indexList.addContentsItem(hasChildren,cd->displayName(),cd->getReference(),cd->getOutputFileBase(),cd->anchor());
      }
      if (hasChildren)
      {
        writeClassTree(cd->subClasses(),level+1);
      }
      cd->visited=TRUE;
    }
  }
  if (started) 
  {
    Doxygen::indexList.decContentsDepth();
  }
}

//----------------------------------------------------------------------------
/*! Generates HTML Help tree of classes */

void writeClassTreeNode(ClassDef *cd,bool &started,int level)
{
  //printf("writeClassTreeNode(%s) visited=%d\n",cd->name().data(),cd->visited);
  static bool vhdl=Config_getBool("OPTIMIZE_OUTPUT_VHDL");

  if (cd->isVisibleInHierarchy() && !cd->visited)
  {
    if (!started)
    {
      started=TRUE;
    }
    bool hasChildren = classHasVisibleChildren(cd);
    //printf("node: Has children %s: %d\n",cd->name().data(),hasChildren);
    if (cd->isLinkable())
    {
      Doxygen::indexList.addContentsItem(hasChildren,cd->displayName(),cd->getReference(),cd->getOutputFileBase(),cd->anchor());
    }
    if (hasChildren)
    {
      if (vhdl)
      {
        writeClassTree(cd->baseClasses(),level+1);
      }
      else
      {
        writeClassTree(cd->subClasses(),level+1);
      }
    }
    cd->visited=TRUE;
  }
}

void writeClassTree(ClassList *cl,int level)
{
  if (cl==0) return;
  ClassListIterator cli(*cl);
  bool started=FALSE;
  for ( cli.toFirst() ; cli.current() ; ++cli)
  {
    cli.current()->visited=FALSE;
  }
  for ( cli.toFirst() ; cli.current() ; ++cli)
  {
    writeClassTreeNode(cli.current(),started,level);
  }
}

void writeClassTree(ClassSDict *d,int level)
{
  if (d==0) return;
  ClassSDict::Iterator cli(*d);
  bool started=FALSE;
  for ( cli.toFirst() ; cli.current() ; ++cli)
  {
    cli.current()->visited=FALSE;
  }
  for ( cli.toFirst() ; cli.current() ; ++cli)
  {
    writeClassTreeNode(cli.current(),started,level);
  }
}

//----------------------------------------------------------------------------

static void writeClassTreeForList(OutputList &ol,ClassSDict *cl,bool &started,FTVHelp* ftv)
{
  static bool vhdl=Config_getBool("OPTIMIZE_OUTPUT_VHDL");
  ClassSDict::Iterator cli(*cl);
  for (;cli.current(); ++cli)
  {
    ClassDef *cd=cli.current();
    //printf("class %s hasVisibleRoot=%d isVisibleInHierarchy=%d\n",
    //             cd->name().data(),
    //              hasVisibleRoot(cd->baseClasses()),
    //              cd->isVisibleInHierarchy()
    //      );
    bool b;
    if (vhdl)
    {
      if ((VhdlDocGen::VhdlClasses)cd->protection()==VhdlDocGen::PACKAGECLASS || 
          (VhdlDocGen::VhdlClasses)cd->protection()==VhdlDocGen::PACKBODYCLASS)
      {
        continue;
      }
      b=!hasVisibleRoot(cd->subClasses());
    }
    else
    {
      b=!hasVisibleRoot(cd->baseClasses());
    }

    if (b)  //filter on root classes
    {
      if (cd->isVisibleInHierarchy()) // should it be visible
      {
        if (!started)
        {
          startIndexHierarchy(ol,0);
          Doxygen::indexList.incContentsDepth();
          started=TRUE;
        }
        ol.startIndexListItem();
        bool hasChildren = !cd->visited && classHasVisibleChildren(cd); 
        //printf("list: Has children %s: %d\n",cd->name().data(),hasChildren);
        if (cd->isLinkable())
        {
          //printf("Writing class %s isLinkable()=%d isLinkableInProject()=%d cd->templateMaster()=%p\n",
          //    cd->displayName().data(),cd->isLinkable(),cd->isLinkableInProject(),cd->templateMaster());
          ol.startIndexItem(cd->getReference(),cd->getOutputFileBase());
          ol.parseText(cd->displayName());
          ol.endIndexItem(cd->getReference(),cd->getOutputFileBase());
          if (cd->isReference()) 
          {
            ol.startTypewriter(); 
            ol.docify(" [external]");
            ol.endTypewriter();
          }
          Doxygen::indexList.addContentsItem(hasChildren,cd->displayName(),cd->getReference(),cd->getOutputFileBase(),cd->anchor());
          if (ftv)
            ftv->addContentsItem(hasChildren,cd->displayName(),cd->getReference(),cd->getOutputFileBase(),cd->anchor()); 
        }
        else
        {
          ol.startIndexItem(0,0);
          ol.parseText(cd->displayName());
          ol.endIndexItem(0,0);
          Doxygen::indexList.addContentsItem(hasChildren,cd->displayName(),0,0,0);
          if (ftv)
            ftv->addContentsItem(hasChildren,cd->displayName(),0,0,0); 
        }
        if (vhdl && hasChildren) 
        {
          writeClassTree(ol,cd->baseClasses(),cd->visited,1,ftv);
          cd->visited=TRUE;
        }
        else if (hasChildren)
        {
          writeClassTree(ol,cd->subClasses(),cd->visited,1,ftv);
          cd->visited=TRUE;
        }
        ol.endIndexListItem();
      }
    }
  }
}

void writeClassHierarchy(OutputList &ol, FTVHelp* ftv)
{
  initClassHierarchy(Doxygen::classSDict);
  initClassHierarchy(Doxygen::hiddenClasses);
  if (ftv)
  {
    ol.pushGeneratorState(); 
    ol.disable(OutputGenerator::Html);
  }
  bool started=FALSE;
  writeClassTreeForList(ol,Doxygen::classSDict,started,ftv);
  writeClassTreeForList(ol,Doxygen::hiddenClasses,started,ftv);
  if (started) 
  {
    endIndexHierarchy(ol,0);
    Doxygen::indexList.decContentsDepth();
  }
  if (ftv)
    ol.popGeneratorState(); 
}

//----------------------------------------------------------------------------

static int countClassesInTreeList(const ClassSDict &cl)
{
  int count=0;
  ClassSDict::Iterator cli(cl);
  for (;cli.current(); ++cli)
  {
    ClassDef *cd=cli.current();
    if (!hasVisibleRoot(cd->baseClasses())) // filter on root classes
    {
      if (cd->isVisibleInHierarchy()) // should it be visible
      {
        if (cd->subClasses()) // should have sub classes
        {
          count++;
        }
      }
    }
  }
  return count;
}

int countClassHierarchy()
{
  int count=0;
  initClassHierarchy(Doxygen::classSDict);
  initClassHierarchy(Doxygen::hiddenClasses);
  count+=countClassesInTreeList(*Doxygen::classSDict);
  count+=countClassesInTreeList(*Doxygen::hiddenClasses);
  return count;
}

//----------------------------------------------------------------------------

void writeHierarchicalIndex(OutputList &ol)
{
  if (hierarchyClasses==0) return;
  ol.pushGeneratorState();
  ol.disable(OutputGenerator::Man);
  LayoutNavEntry *lne = LayoutDocManager::instance().rootNavEntry()->find(LayoutNavEntry::ClassHierarchy);
  QCString title = lne->title();
  startFile(ol,"hierarchy",0, title, HLI_Hierarchy);
  startTitle(ol,0);
  ol.parseText(title);
  endTitle(ol,0,0);
  ol.startContents();
  ol.startTextBlock();
  Doxygen::indexList.addContentsItem(TRUE,title,0,"hierarchy",0); 
  if (Config_getBool("HAVE_DOT") && Config_getBool("GRAPHICAL_HIERARCHY"))
  {
    ol.disable(OutputGenerator::Latex);
    ol.disable(OutputGenerator::RTF);
    ol.startParagraph();
    ol.startTextLink("inherits",0);
    ol.parseText(theTranslator->trGotoGraphicalHierarchy());
    ol.endTextLink();
    ol.endParagraph();
    ol.enable(OutputGenerator::Latex);
    ol.enable(OutputGenerator::RTF);
  }
  ol.parseText(lne->intro());
  ol.endTextBlock();

  FTVHelp* ftv = 0;
  bool treeView=Config_getBool("USE_INLINE_TREES");
  if (treeView)
  {
    ftv = new FTVHelp(FALSE);
  }

  writeClassHierarchy(ol,ftv);

  if (ftv)
  {
    QGString outStr;
    FTextStream t(&outStr);
    ftv->generateTreeViewInline(t);
    ol.pushGeneratorState(); 
    ol.disableAllBut(OutputGenerator::Html);
    ol.writeString(outStr);
    ol.popGeneratorState();
    delete ftv;
  }
  endFile(ol);
  ol.popGeneratorState();
}

//----------------------------------------------------------------------------

void writeGraphicalClassHierarchy(OutputList &ol)
{
  if (hierarchyClasses==0) return;
  ol.disableAllBut(OutputGenerator::Html);
  LayoutNavEntry *lne = LayoutDocManager::instance().rootNavEntry()->find(LayoutNavEntry::ClassHierarchy);
  QCString title = lne->title();
  startFile(ol,"inherits",0,title,HLI_Hierarchy,FALSE,"hierarchy");
  startTitle(ol,0);
  //if (!Config_getString("PROJECT_NAME").isEmpty()) 
  //{
  //  title.prepend(Config_getString("PROJECT_NAME")+" ");
  //}
  ol.parseText(title);
  endTitle(ol,0,0);
  ol.startContents();
  ol.startTextBlock();
  //Doxygen::indexList.addContentsItem(FALSE,theTranslator->trGraphicalHierarchy(),0,"inherits",0); 
  ol.startParagraph();
  ol.startTextLink("hierarchy",0);
  ol.parseText(theTranslator->trGotoTextualHierarchy());
  ol.endTextLink();
  ol.endParagraph();
  //parseText(ol,theTranslator->trClassHierarchyDescription());
  //ol.newParagraph();
  ol.endTextBlock();
  DotGfxHierarchyTable g;
  ol.writeGraphicalHierarchy(g);
  endFile(ol);
  ol.enableAll();
}

//----------------------------------------------------------------------------

void countFiles(int &htmlFiles,int &files)
{
  htmlFiles=0;
  files=0;
  FileNameListIterator fnli(*Doxygen::inputNameList);
  FileName *fn;
  for (;(fn=fnli.current());++fnli)
  {
    FileNameIterator fni(*fn);
    FileDef *fd;
    for (;(fd=fni.current());++fni)
    {
      bool doc = fd->isLinkableInProject();
      bool src = fd->generateSourceFile();
      bool nameOk = !fd->isDocumentationFile();
      if (nameOk)
      {
        if (doc || src)
        {
          htmlFiles++;
        }
        if (doc)
        {
          files++;
        }
      }
    }
  }
}

//----------------------------------------------------------------------------

void writeFileIndex(OutputList &ol)
{
  if (documentedHtmlFiles==0) return;
  ol.pushGeneratorState();
  ol.disable(OutputGenerator::Man);
  if (documentedFiles==0) ol.disableAllBut(OutputGenerator::Html);
  LayoutNavEntry *lne = LayoutDocManager::instance().rootNavEntry()->find(LayoutNavEntry::Files);
  QCString title = lne->title();
  startFile(ol,"files",0,title,HLI_Files);
  startTitle(ol,0);
  //if (!Config_getString("PROJECT_NAME").isEmpty()) 
  //{
  //  title.prepend(Config_getString("PROJECT_NAME")+" ");
  //}
  ol.parseText(title);
  endTitle(ol,0,0);
  ol.startContents();
  ol.startTextBlock();
  Doxygen::indexList.addContentsItem(TRUE,title,0,"files",0); 
  Doxygen::indexList.incContentsDepth();
  ol.parseText(lne->intro());
  ol.endTextBlock();

  OutputNameDict outputNameDict(1009);
  OutputNameList outputNameList;
  outputNameList.setAutoDelete(TRUE);
  
  if (Config_getBool("FULL_PATH_NAMES"))
  {
    // re-sort input files in (dir,file) output order instead of (file,dir) input order 
    FileName *fn=Doxygen::inputNameList->first();
    while (fn)
    {
      FileDef *fd=fn->first();
      while (fd)
      {
        QCString path=fd->getPath();
        if (path.isEmpty()) path="[external]";
        FileList *fl = outputNameDict.find(path);
        if (fl)
        {
          fl->inSort(fd);
          //printf("+ inserting %s---%s\n",fd->getPath().data(),fd->name().data());
        }
        else
        {
          //printf("o inserting %s---%s\n",fd->getPath().data(),fd->name().data());
          fl = new FileList(path);
          fl->inSort(fd);
          outputNameList.inSort(fl);
          outputNameDict.insert(path,fl);
        }
        fd=fn->next();
      }
      fn=Doxygen::inputNameList->next();
    }
  }
  
  ol.startIndexList();
  FileList *fl=0;
  if (Config_getBool("FULL_PATH_NAMES"))
  {
    fl = outputNameList.first();
  }
  else
  {
    fl = Doxygen::inputNameList->first();
  }
  while (fl)
  {
    FileDef *fd=fl->first();
    while (fd)
    {
      //printf("Found filedef %s\n",fd->name().data());
      bool doc = fd->isLinkableInProject();
      bool src = fd->generateSourceFile();
      bool nameOk = !fd->isDocumentationFile();
      if (nameOk && (doc || src) && 
              !fd->isReference())
      {
        QCString path;
        if (Config_getBool("FULL_PATH_NAMES")) 
        {
          path=stripFromPath(fd->getPath().copy());
        }
        QCString fullName=fd->name();
        if (!path.isEmpty()) 
        {
          if (path.at(path.length()-1)!='/') fullName.prepend("/");
          fullName.prepend(path);
        }

        ol.startIndexKey();
        ol.docify(path);
        if (doc)
        {
          ol.writeObjectLink(0,fd->getOutputFileBase(),0,fd->name());
          Doxygen::indexList.addContentsItem(FALSE,fullName,fd->getReference(),fd->getOutputFileBase(),0);
        }
        else
        {
          ol.startBold();
          ol.docify(fd->name());
          ol.endBold();
          Doxygen::indexList.addContentsItem(FALSE,fullName,0,0,0);
        }
        if (src)
        {
          ol.pushGeneratorState();
          ol.disableAllBut(OutputGenerator::Html);
          ol.docify(" ");
          ol.startTextLink(fd->includeName(),0);
          ol.docify("[");
          ol.parseText(theTranslator->trCode());
          ol.docify("]");
          ol.endTextLink();
          ol.popGeneratorState();
        }
        ol.endIndexKey();
        bool hasBrief = !fd->briefDescription().isEmpty();
        ol.startIndexValue(hasBrief);
        if (hasBrief)
        {
          //ol.docify(" (");
          ol.parseDoc(
              fd->briefFile(),fd->briefLine(),
              fd,0,
              abbreviate(fd->briefDescription(),fd->name()),
              FALSE, // index words
              FALSE, // isExample
              0,     // example name
              TRUE,  // single line
              TRUE   // link from index
             );
          //ol.docify(")");
        }
        ol.endIndexValue(fd->getOutputFileBase(),hasBrief);
        //ol.popGeneratorState();
        // --------------------------------------------------------
      }
      fd=fl->next();
    }
    if (Config_getBool("FULL_PATH_NAMES"))
    {
      fl=outputNameList.next();
    }
    else
    {
      fl=Doxygen::inputNameList->next();
    }
  }
  ol.endIndexList();
  Doxygen::indexList.decContentsDepth();
  endFile(ol);
  ol.popGeneratorState();
}

//----------------------------------------------------------------------------
int countNamespaces()
{
  int count=0;
  NamespaceSDict::Iterator nli(*Doxygen::namespaceSDict);
  NamespaceDef *nd;
  for (;(nd=nli.current());++nli)
  {
    if (nd->isLinkableInProject()) count++;
  }
  return count;
}

//----------------------------------------------------------------------------

void writeNamespaceIndex(OutputList &ol)
{
  if (documentedNamespaces==0) return;
  ol.pushGeneratorState();
  ol.disable(OutputGenerator::Man);
  LayoutNavEntry *lne = LayoutDocManager::instance().rootNavEntry()->find(LayoutNavEntry::Namespaces);
  QCString title = lne->title();
  startFile(ol,"namespaces",0,title,HLI_Namespaces);
  startTitle(ol,0);
  //if (!Config_getString("PROJECT_NAME").isEmpty()) 
  //{
  //  longTitle.prepend(Config_getString("PROJECT_NAME")+" ");
  //}
  ol.parseText(title);
  endTitle(ol,0,0);
  ol.startContents();
  ol.startTextBlock();
  Doxygen::indexList.addContentsItem(TRUE,title,0,"namespaces",0); 
  Doxygen::indexList.incContentsDepth();
  ol.parseText(lne->intro());
  ol.endTextBlock();

  bool first=TRUE;

  NamespaceSDict::Iterator nli(*Doxygen::namespaceSDict);
  NamespaceDef *nd;
  for (nli.toFirst();(nd=nli.current());++nli)
  {
    if (nd->isLinkableInProject())
    {
      if (first)
      {
        ol.startIndexList();
        first=FALSE;
      }
      //ol.writeStartAnnoItem("namespace",nd->getOutputFileBase(),0,nd->name());
      ol.startIndexKey();
      ol.writeObjectLink(0,nd->getOutputFileBase(),0,nd->displayName());
      ol.endIndexKey();
      bool hasBrief = !nd->briefDescription().isEmpty();
      ol.startIndexValue(hasBrief);
      if (hasBrief)
      {
        //ol.docify(" (");
        ol.parseDoc(
                 nd->briefFile(),nd->briefLine(),
                 nd,0,
                 abbreviate(nd->briefDescription(),nd->displayName()),
                 FALSE, // index words
                 FALSE, // isExample
                 0,     // example name
                 TRUE,  // single line
                 TRUE   // link from index
                );
        //ol.docify(")");
      }
      ol.endIndexValue(nd->getOutputFileBase(),hasBrief);
      //ol.writeEndAnnoItem(nd->getOutputFileBase());
      Doxygen::indexList.addContentsItem(FALSE,nd->displayName(),nd->getReference(),nd->getOutputFileBase(),0);
    }
  }
  if (!first) ol.endIndexList();
  Doxygen::indexList.decContentsDepth();
  endFile(ol);
  ol.popGeneratorState();
}

//----------------------------------------------------------------------------

int countAnnotatedClasses(int *cp)
{
  int count=0;
  int countPrinted=0;
  ClassSDict::Iterator cli(*Doxygen::classSDict);
  ClassDef *cd;
  for (;(cd=cli.current());++cli)
  {
    if (cd->isLinkableInProject() && cd->templateMaster()==0) 
    { 
      if (!cd->isEmbeddedInOuterScope())
      {
        countPrinted++;
      }
      count++; 
    }
  }
  *cp = countPrinted;
  return count;
}


//----------------------------------------------------------------------

void writeAnnotatedClassList(OutputList &ol)
{
  ol.startIndexList(); 
  ClassSDict::Iterator cli(*Doxygen::classSDict);
  ClassDef *cd;
  
  // clear index
  int x,y;
  for (y=0;y<CHL_Total;y++)
  {
    for (x=0;x<256;x++)
    {
      g_classIndexLetterUsed[y][x]=FALSE;
    }
  }
  
  // see which elements are in use
  for (cli.toFirst();(cd=cli.current());++cli)
  {
    if (cd->isLinkableInProject() && cd->templateMaster()==0)
    {
      QCString dispName = cd->displayName();
      int c = dispName.at(getPrefixIndex(dispName));
      g_classIndexLetterUsed[CHL_All][c]=TRUE;
      switch(cd->compoundType())
      {
        case ClassDef::Class:
          g_classIndexLetterUsed[CHL_Classes][c]=TRUE;
          break;
        case ClassDef::Struct:
          g_classIndexLetterUsed[CHL_Structs][c]=TRUE;
          break;
        case ClassDef::Union:
          g_classIndexLetterUsed[CHL_Unions][c]=TRUE;
          break;
        case ClassDef::Interface:
          g_classIndexLetterUsed[CHL_Interfaces][c]=TRUE;
          break;
        case ClassDef::Protocol:
          g_classIndexLetterUsed[CHL_Protocols][c]=TRUE;
          break;
        case ClassDef::Category:
          g_classIndexLetterUsed[CHL_Categories][c]=TRUE;
          break;
        case ClassDef::Exception:
          g_classIndexLetterUsed[CHL_Exceptions][c]=TRUE;
          break;

      }
    }
  }
  
  for (cli.toFirst();(cd=cli.current());++cli)
  {
    ol.pushGeneratorState();
    if (cd->isEmbeddedInOuterScope())
    {
      ol.disable(OutputGenerator::Latex);
      ol.disable(OutputGenerator::RTF);
    }
    if (cd->isLinkableInProject() && cd->templateMaster()==0)
    {
      QCString type=cd->compoundTypeString();
      ol.startIndexKey();
      static bool vhdl = Config_getBool("OPTIMIZE_OUTPUT_VHDL");
      if (vhdl)
      {
        QCString prot= VhdlDocGen::getProtectionName((VhdlDocGen::VhdlClasses)cd->protection());
        ol.docify(prot.data());
        ol.writeString(" ");
        ol.insertMemberAlign();
      }
      ol.writeObjectLink(0,cd->getOutputFileBase(),cd->anchor(),cd->displayName());
      ol.endIndexKey();
      bool hasBrief = !cd->briefDescription().isEmpty();
      ol.startIndexValue(hasBrief);
      if (hasBrief)
      {
        ol.parseDoc(
                 cd->briefFile(),cd->briefLine(),
                 cd,0,
                 abbreviate(cd->briefDescription(),cd->displayName()),
                 FALSE,  // indexWords
                 FALSE,  // isExample
                 0,     // example name
                 TRUE,  // single line
                 TRUE   // link from index
                );
      }
      ol.endIndexValue(cd->getOutputFileBase(),hasBrief);
      //ol.writeEndAnnoItem(cd->getOutputFileBase());
      Doxygen::indexList.addContentsItem(FALSE,cd->displayName(),cd->getReference(),cd->getOutputFileBase(),cd->anchor());
    }
    ol.popGeneratorState();
  }
  ol.endIndexList();
}

static QCString letterToLabel(char startLetter)
{
  QCString s(5); 
  if (isId(startLetter))
  {
    s[0]=startLetter; s[1]=0;
  }
  else
  {
    const char hex[]="0123456789abcdef";
    s[0]='0';
    s[1]='x';
    s[2]=hex[startLetter>>4];
    s[3]=hex[startLetter&0xF];
    s[4]=0;
  }
  return s;
}

//----------------------------------------------------------------------------

class PrefixIgnoreClassList : public ClassList
{
public:
  virtual int compareItems(GCI item1, GCI item2)
  {
    ClassDef *c1=(ClassDef *)item1;
    ClassDef *c2=(ClassDef *)item2;

    QCString n1 = c1->className();
    QCString n2 = c2->className();
    return stricmp (n1.data()+getPrefixIndex(n1), n2.data()+getPrefixIndex(n2));
  }
};

class AlphaIndexTableCell
{
  public:
    AlphaIndexTableCell(int row,int col,uchar letter,ClassDef *cd) : 
      m_letter(letter), m_class(cd), m_row(row), m_col(col) 
    { //printf("AlphaIndexTableCell(%d,%d,%c,%s)\n",row,col,letter!=0 ? letter: '-',
      //       cd!=(ClassDef*)0x8 ? cd->name().data() : "<null>"); 
    }

    ClassDef *classDef() const { return m_class; }
    uchar letter()       const { return m_letter; }
    int row()            const { return m_row; }
    int column()         const { return m_col; }

  private:
    uchar m_letter;
    ClassDef *m_class;
    int m_row;
    int m_col;
};

class AlphaIndexTableRows : public QList<AlphaIndexTableCell>
{
  public:
    AlphaIndexTableRows() { setAutoDelete(TRUE); }
};

class AlphaIndexTableRowsIterator : public QListIterator<AlphaIndexTableCell>
{
  public:
    AlphaIndexTableRowsIterator(const AlphaIndexTableRows &list) : 
      QListIterator<AlphaIndexTableCell>(list) {}
};

class AlphaIndexTableColumns : public QList<AlphaIndexTableRows>
{
  public:
    AlphaIndexTableColumns() { setAutoDelete(TRUE); }
};

// write an alphabetical index of all class with a header for each letter
void writeAlphabeticalClassList(OutputList &ol)
{
  //ol.startAlphabeticalIndexList(); 
  // What starting letters are used
  bool indexLetterUsed[256];
  memset (indexLetterUsed, 0, sizeof (indexLetterUsed));

  // first count the number of headers
  ClassSDict::Iterator cli(*Doxygen::classSDict);
  ClassDef *cd;
  uint startLetter=0;
  int headerItems=0;
  for (;(cd=cli.current());++cli)
  {
    if (cd->isLinkableInProject() && cd->templateMaster()==0)
    {
      int index = getPrefixIndex(cd->className());
      //printf("name=%s index=%d\n",cd->className().data(),index);
      startLetter=toupper(cd->className().at(index))&0xFF;
      indexLetterUsed[startLetter] = true;
    }
  }

  QCString alphaLinks = "<div class=\"qindex\">";
  int l;
  for (l=0; l<256; l++)
  {
    if (indexLetterUsed[l])
    {
      if (headerItems) alphaLinks += "&#160;|&#160;";
      headerItems++;
      alphaLinks += (QCString)"<a class=\"qindex\" href=\"#letter_" + 
                    (char)l + "\">" + 
                    (char)l + "</a>";
    }
  }

  alphaLinks += "</div>\n";
  ol.writeString(alphaLinks);


  // the number of columns in the table
  const int columns = Config_getInt("COLS_IN_ALPHA_INDEX");

  int i,j;
  int totalItems = headerItems*2 + annotatedClasses;          // number of items in the table (headers span 2 items)
  int rows = (totalItems + columns - 1)/columns;          // number of rows in the table
  //int itemsInLastRow = (totalItems + columns -1)%columns + 1; // number of items in the last row

  //printf("headerItems=%d totalItems=%d columns=%d rows=%d itemsInLastRow=%d\n",
  //    headerItems,totalItems,columns,rows,itemsInLastRow);

  // Keep a list of classes for each starting letter
  PrefixIgnoreClassList classesByLetter[256];
  AlphaIndexTableColumns tableColumns;

  // fill the columns with the class list (row elements in each column,
  // expect for the columns with number >= itemsInLastRow, which get one
  // item less.
  //int icount=0;
  startLetter=0;
  for (cli.toFirst();(cd=cli.current());++cli)
  {
    if (cd->isLinkableInProject() && cd->templateMaster()==0)
    {
      int index = getPrefixIndex(cd->className());
      startLetter=toupper(cd->className().at(index))&0xFF;
      // Do some sorting again, since the classes are sorted by name with 
      // prefix, which should be ignored really.
      classesByLetter[startLetter].inSort(cd);
    }
  }

  #define NEXT_ROW()                           \
    do                                         \
    {                                          \
      if (row>maxRows) maxRows=row;            \
      if (row>=rows && col<columns)            \
      {                                        \
        col++;                                 \
        row=0;                                 \
        tableRows = new AlphaIndexTableRows;   \
        tableColumns.append(tableRows);        \
      }                                        \
    }                                          \
    while(0)                                   \

  AlphaIndexTableRows *tableRows = new AlphaIndexTableRows;
  tableColumns.append(tableRows);
  int col=0,row=0,maxRows=0;
  for (l=0; l<256; l++)
  {
    if (classesByLetter[l].count()>0)
    {
      // add special header cell
      tableRows->append(new AlphaIndexTableCell(row,col,(uchar)l,(ClassDef*)0x8));
      row++;
      tableRows->append(new AlphaIndexTableCell(row,col,0,(ClassDef*)0x8));
      row++;
      tableRows->append(new AlphaIndexTableCell(row,col,0,classesByLetter[l].at(0)));
      row++; 
      NEXT_ROW();
      for (i=1; i<(int)classesByLetter[l].count(); i++)
      {
        // add normal cell
        tableRows->append(new AlphaIndexTableCell(row,col,0,classesByLetter[l].at(i)));
        row++;
        NEXT_ROW();
      }
    }
  }

  // create row iterators for each column
  AlphaIndexTableRowsIterator **colIterators = new AlphaIndexTableRowsIterator*[columns];
  for (i=0;i<columns;i++)
  {
    if (i<(int)tableColumns.count())
    {
      colIterators[i] = new AlphaIndexTableRowsIterator(*tableColumns.at(i));
    }
    else // empty column
    {
      colIterators[i] = 0;
    }
  }

  ol.writeString("<table style=\"margin: 10px;\" align=\"center\" width=\"95%\" border=\"0\" cellspacing=\"0\" cellpadding=\"0\">\n");
  // generate table
  for (i=0;i<=maxRows;i++) // foreach table row
  {
    //printf("writing row %d\n",i);
    //ol.nextTableRow();
    ol.writeString("<tr>");
    // the last column may contain less items then the others
    //int colsInRow = (i<rows-1) ? columns : itemsInLastRow; 
    //printf("row [%d]\n",i);
    for (j=0;j<columns;j++) // foreach table column
    {
      if (colIterators[j])
      {
        AlphaIndexTableCell *cell = colIterators[j]->current();
        if (cell)
        {
          if (cell->row()==i)
          {
            if (cell->letter()!=0)
            {
              QCString s = letterToLabel(cell->letter());
              ol.writeString("<td rowspan=\"2\" valign=\"bottom\">");
              ol.writeString("<a name=\"letter_");
              ol.writeString(s);
              ol.writeString("\"></a>");
              ol.writeString("<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\">"
                  "<tr>"
                  "<td><div class=\"ah\">&#160;&#160;"); 
              ol.writeString(s);
              ol.writeString(         "&#160;&#160;</div>"
                  "</td>"
                  "</tr>"
                  "</table>\n");
            }
            else if (cell->classDef()!=(ClassDef*)0x8)
            {
              cd = cell->classDef();
              ol.writeString("<td valign=\"top\">");
              QCString namesp,cname;
              //if (cd->getNamespaceDef()) namesp=cd->getNamespaceDef()->displayName();
              //QCString cname=cd->className();
              extractNamespaceName(cd->name(),cname,namesp);
              QCString nsDispName;
              if (Config_getBool("OPTIMIZE_OUTPUT_JAVA"))
              {
                nsDispName=substitute(namesp,"::",".");
              }
              else
              {
                nsDispName=namesp.copy();
              }
              if (cname.right(2)=="-g" || cname.right(2)=="-p")
              {
                cname = cname.left(cname.length()-2);
              }

              ol.writeObjectLink(cd->getReference(),
                  cd->getOutputFileBase(),cd->anchor(),cname);
              if (!namesp.isEmpty())
              {
                ol.docify(" (");
                NamespaceDef *nd = getResolvedNamespace(namesp);
                if (nd && nd->isLinkable())
                {
                  ol.writeObjectLink(nd->getReference(),
                      nd->getOutputFileBase(),0,nsDispName);
                }
                else
                {
                  ol.docify(nsDispName);
                }
                ol.docify(")");
              }
              ol.writeNonBreakableSpace(3);
            }
            ++(*colIterators[j]);
            if (cell->letter()!=0 || cell->classDef()!=(ClassDef*)0x8)
            {
              ol.writeString("</td>");
            }
          }
        }
        else
        {
          ol.writeString("<td></td>");
        }
      }
    }
    ol.writeString("</tr>\n");
  }
  ol.writeString("</table>\n");
  
  ol.writeString(alphaLinks);

  // release the temporary memory
  for (i=0;i<columns;i++)
  {
    delete colIterators[i];
  }
  delete[] colIterators;
  //delete[] colList;
}

//----------------------------------------------------------------------------

void writeAlphabeticalIndex(OutputList &ol)
{
  if (annotatedClasses==0) return;
  ol.pushGeneratorState();
  ol.disableAllBut(OutputGenerator::Html);
  LayoutNavEntry *lne = LayoutDocManager::instance().rootNavEntry()->find(LayoutNavEntry::Classes);
  QCString title = lne->title();
  startFile(ol,"classes",0,title,HLI_Classes); 
  startTitle(ol,0);
  ol.parseText(title);
  Doxygen::indexList.addContentsItem(TRUE,title,0,"classes",0); 
  //ol.parseText(/*Config_getString("PROJECT_NAME")+" "+*/
  //             (fortranOpt ? theTranslator->trCompoundIndexFortran() :
  //              vhdlOpt    ? VhdlDocGen::trDesignUnitIndex()             :
  //                           theTranslator->trCompoundIndex()
  //             ));
  endTitle(ol,0,0);
  ol.startContents();
  writeAlphabeticalClassList(ol);
  endFile(ol);
  ol.popGeneratorState();
}

//----------------------------------------------------------------------------

void writeAnnotatedIndex(OutputList &ol)
{
  //printf("writeAnnotatedIndex: count=%d printed=%d\n",
  //    annotatedClasses,annotatedClassesPrinted);
  if (annotatedClasses==0) return;
  
  ol.pushGeneratorState();
  ol.disable(OutputGenerator::Man);
  if (annotatedClassesPrinted==0)
  {
    ol.disable(OutputGenerator::Latex);
    ol.disable(OutputGenerator::RTF);
  }
  LayoutNavEntry *lne = LayoutDocManager::instance().rootNavEntry()->find(LayoutNavEntry::ClassAnnotated);
  QCString title = lne->title();
  startFile(ol,"annotated",0,title,HLI_Annotated);
  startTitle(ol,0);
  //QCString longTitle = title;
  //if (!Config_getString("PROJECT_NAME").isEmpty()) 
  //{
  //  longTitle.prepend(Config_getString("PROJECT_NAME")+" ");
  //}
  ol.parseText(title);
  endTitle(ol,0,0);
  ol.startContents();
  ol.startTextBlock();
  Doxygen::indexList.addContentsItem(TRUE,title,0,"annotated",0); 
  Doxygen::indexList.incContentsDepth();
  ol.parseText(lne->intro());
  ol.endTextBlock();
  writeAnnotatedClassList(ol);
  Doxygen::indexList.decContentsDepth();
  
  endFile(ol);
  ol.popGeneratorState();
}

//----------------------------------------------------------------------------
static void writeClassLinkForMember(OutputList &ol,MemberDef *md,const char *separator,
                             QCString &prevClassName)
{
  ClassDef *cd=md->getClassDef();
  if ( cd && prevClassName!=cd->displayName())
  {
    ol.docify(separator);
    ol.writeObjectLink(md->getReference(),md->getOutputFileBase(),md->anchor(),
        cd->displayName());
    ol.writeString("\n");
    prevClassName = cd->displayName();
  }
}

static void writeFileLinkForMember(OutputList &ol,MemberDef *md,const char *separator,
                             QCString &prevFileName)
{
  FileDef *fd=md->getFileDef();
  if (fd && prevFileName!=fd->name())
  {
    ol.docify(separator);
    ol.writeObjectLink(md->getReference(),md->getOutputFileBase(),md->anchor(),
        fd->name());
    ol.writeString("\n");
    prevFileName = fd->name();
  }
}

static void writeNamespaceLinkForMember(OutputList &ol,MemberDef *md,const char *separator,
                             QCString &prevNamespaceName)
{
  NamespaceDef *nd=md->getNamespaceDef();
  if (nd && prevNamespaceName!=nd->name())
  {
    ol.docify(separator);
    ol.writeObjectLink(md->getReference(),md->getOutputFileBase(),md->anchor(),
        nd->name());
    ol.writeString("\n");
    prevNamespaceName = nd->name();
  }
}

static void writeMemberList(OutputList &ol,bool useSections,int page,
                            MemberIndexList memberLists[MEMBER_INDEX_ENTRIES],
                            DefinitionIntf::DefType type)
{
  int pi;
  // page==-1 => write all member indices to one page (used when total members is small)
  // page!=-1 => write all member for this page only (used when total member is large)
  int startIndex = page==-1 ? 0                      : page;
  int endIndex   = page==-1 ? MEMBER_INDEX_ENTRIES-1 : page;
  ASSERT((int)type<3);

  typedef void (*writeLinkForMember_t)(OutputList &ol,MemberDef *md,const char *separator,
                                   QCString &prevNamespaceName);

  // each index tab has its own write function
  static writeLinkForMember_t writeLinkForMemberMap[3] = 
  { 
    &writeClassLinkForMember, 
    &writeFileLinkForMember,
    &writeNamespaceLinkForMember
  };
  QCString prevName;
  QCString prevDefName;
  bool first=TRUE;
  bool firstSection=TRUE;
  bool firstItem=TRUE;
  for (pi=startIndex; pi<=endIndex; pi++) // page==-1 => pi=[0..127], page!=-1 => pi=page 
  {
    MemberIndexList *ml = &memberLists[pi];
    if (ml->count()==0) continue;
    ml->sort();
    QListIterator<MemberDef> mli(*ml);
    MemberDef *md;
    for (mli.toFirst();(md=mli.current());++mli)
    {
      const char *sep;
      bool isFunc=!md->isObjCMethod() && 
        (md->isFunction() || md->isSlot() || md->isSignal()); 
      QCString name=md->name();
      int startIndex = getPrefixIndex(name);
      if (QCString(name.data()+startIndex)!=prevName) // new entry
      {
        if ((prevName.isEmpty() || 
            tolower(name.at(startIndex))!=tolower(prevName.at(0))) && 
            useSections) // new section
        {
          if (!firstItem)    ol.endItemListItem();
          if (!firstSection) ol.endItemList();
          char cl[2];
          cl[0] = tolower(name.at(startIndex));
          cl[1] = 0;
          QCString cs = letterToLabel(cl[0]);
          QCString anchor=(QCString)"index_"+cs;
          QCString title=(QCString)"- "+cl+" -";
          ol.startSection(anchor,title,SectionInfo::Subsection);
          ol.docify(title);
          ol.endSection(anchor,SectionInfo::Subsection);
          ol.startItemList();
          firstSection=FALSE;
          firstItem=TRUE;
        }
        else if (!useSections && first)
        {
          ol.startItemList();
          first=FALSE;
        }

        // member name
        if (!firstItem) ol.endItemListItem();
        ol.startItemListItem();
        firstItem=FALSE;
        ol.docify(name);
        if (isFunc) ol.docify("()");
        ol.writeString("\n");

        // link to class
        prevDefName="";
        sep = ": ";
        prevName = name.data()+startIndex;
      }
      else // same entry
      {
        sep = ", ";
        // link to class for other members with the same name
      }
      // write the link for the specific list type
      writeLinkForMemberMap[(int)type](ol,md,sep,prevDefName);
    }
  }
  if (!firstItem) ol.endItemListItem();
  ol.endItemList();
}

//----------------------------------------------------------------------------

void initClassMemberIndices()
{
  int i=0;
  int j=0;
  for (j=0;j<CMHL_Total;j++)
  {
    documentedClassMembers[j]=0;
    for (i=0;i<MEMBER_INDEX_ENTRIES;i++) 
    {
      g_memberIndexLetterUsed[j][i].clear();
    }
  }
}

void addClassMemberNameToIndex(MemberDef *md)
{
  static bool hideFriendCompounds = Config_getBool("HIDE_FRIEND_COMPOUNDS");
  static bool vhdlOpt = Config_getBool("OPTIMIZE_OUTPUT_VHDL");
  ClassDef *cd=0;

  if (vhdlOpt && (VhdlDocGen::isRecord(md) || VhdlDocGen::isUnit(md)))
  {
    VhdlDocGen::adjustRecordMember(md);
  }
  
  if (md->isLinkableInProject() && 
      (cd=md->getClassDef())    && 
      cd->isLinkableInProject() &&
      cd->templateMaster()==0)
  {
    QCString n = md->name();
    int index = getPrefixIndex(n);
    uchar charCode = (uchar)n.at(index);
    uint letter = charCode<128 ? tolower(charCode) : charCode;
    if (!n.isEmpty()) 
    {
      bool isFriendToHide = hideFriendCompounds &&
        (QCString(md->typeString())=="friend class" || 
         QCString(md->typeString())=="friend struct" ||
         QCString(md->typeString())=="friend union");
      if (!(md->isFriend() && isFriendToHide))
      {
        g_memberIndexLetterUsed[CMHL_All][letter].append(md);
        documentedClassMembers[CMHL_All]++;
      }
      if (md->isFunction()  || md->isSlot() || md->isSignal())
      {
        g_memberIndexLetterUsed[CMHL_Functions][letter].append(md);
        documentedClassMembers[CMHL_Functions]++;
      } 
      else if (md->isVariable())
      {
        g_memberIndexLetterUsed[CMHL_Variables][letter].append(md);
        documentedClassMembers[CMHL_Variables]++;
      }
      else if (md->isTypedef())
      {
        g_memberIndexLetterUsed[CMHL_Typedefs][letter].append(md);
        documentedClassMembers[CMHL_Typedefs]++;
      }
      else if (md->isEnumerate())
      {
        g_memberIndexLetterUsed[CMHL_Enums][letter].append(md);
        documentedClassMembers[CMHL_Enums]++;
      }
      else if (md->isEnumValue())
      {
        g_memberIndexLetterUsed[CMHL_EnumValues][letter].append(md);
        documentedClassMembers[CMHL_EnumValues]++;
      }
      else if (md->isProperty())
      {
        g_memberIndexLetterUsed[CMHL_Properties][letter].append(md);
        documentedClassMembers[CMHL_Properties]++;
      }
      else if (md->isEvent())
      {
        g_memberIndexLetterUsed[CMHL_Events][letter].append(md);
        documentedClassMembers[CMHL_Events]++;
      }
      else if (md->isRelated() || md->isForeign() ||
               (md->isFriend() && !isFriendToHide))
      {
        g_memberIndexLetterUsed[CMHL_Related][letter].append(md);
        documentedClassMembers[CMHL_Related]++;
      }
    }
  }
}

//----------------------------------------------------------------------------

void initNamespaceMemberIndices()
{
  int i=0;
  int j=0;
  for (j=0;j<NMHL_Total;j++)
  {
    documentedNamespaceMembers[j]=0;
    for (i=0;i<MEMBER_INDEX_ENTRIES;i++) 
    {
      g_namespaceIndexLetterUsed[j][i].clear();
    }
  }
}

void addNamespaceMemberNameToIndex(MemberDef *md)
{
  NamespaceDef *nd=md->getNamespaceDef();
  if (nd && nd->isLinkableInProject() && md->isLinkableInProject())
  {
    QCString n = md->name();
    int index = getPrefixIndex(n);
    uchar charCode = (uchar)n.at(index);
    uint letter = charCode<128 ? tolower(charCode) : charCode;
    if (!n.isEmpty()) 
    {
      g_namespaceIndexLetterUsed[NMHL_All][letter].append(md);
      documentedNamespaceMembers[NMHL_All]++;

      if (md->isFunction()) 
      {
        g_namespaceIndexLetterUsed[NMHL_Functions][letter].append(md);
        documentedNamespaceMembers[NMHL_Functions]++;
      }
      else if (md->isVariable()) 
      {
        g_namespaceIndexLetterUsed[NMHL_Variables][letter].append(md);
        documentedNamespaceMembers[NMHL_Variables]++;
      }
      else if (md->isTypedef())
      {
        g_namespaceIndexLetterUsed[NMHL_Typedefs][letter].append(md);
        documentedNamespaceMembers[NMHL_Typedefs]++;
      }
      else if (md->isEnumerate())
      {
        g_namespaceIndexLetterUsed[NMHL_Enums][letter].append(md);
        documentedNamespaceMembers[NMHL_Enums]++;
      }
      else if (md->isEnumValue())
      {
        g_namespaceIndexLetterUsed[NMHL_EnumValues][letter].append(md);
        documentedNamespaceMembers[NMHL_EnumValues]++;
      }
    }
  }
}

//----------------------------------------------------------------------------

void initFileMemberIndices()
{
  int i=0;
  int j=0;
  for (j=0;j<NMHL_Total;j++)
  {
    documentedFileMembers[j]=0;
    for (i=0;i<MEMBER_INDEX_ENTRIES;i++) 
    {
      g_fileIndexLetterUsed[j][i].clear();
    }
  }
}

void addFileMemberNameToIndex(MemberDef *md)
{
  FileDef *fd=md->getFileDef();
  if (fd && fd->isLinkableInProject() && md->isLinkableInProject())
  {
    QCString n = md->name();
    int index = getPrefixIndex(n);
    uchar charCode = (uchar)n.at(index);
    uint letter = charCode<128 ? tolower(charCode) : charCode;
    if (!n.isEmpty()) 
    {
      g_fileIndexLetterUsed[FMHL_All][letter].append(md);
      documentedFileMembers[FMHL_All]++;

      if (md->isFunction()) 
      {
        g_fileIndexLetterUsed[FMHL_Functions][letter].append(md);
        documentedFileMembers[FMHL_Functions]++;
      }
      else if (md->isVariable()) 
      {
        g_fileIndexLetterUsed[FMHL_Variables][letter].append(md);
        documentedFileMembers[FMHL_Variables]++;
      }
      else if (md->isTypedef())
      {
        g_fileIndexLetterUsed[FMHL_Typedefs][letter].append(md);
        documentedFileMembers[FMHL_Typedefs]++;
      }
      else if (md->isEnumerate())
      {
        g_fileIndexLetterUsed[FMHL_Enums][letter].append(md);
        documentedFileMembers[FMHL_Enums]++;
      }
      else if (md->isEnumValue())
      {
        g_fileIndexLetterUsed[FMHL_EnumValues][letter].append(md);
        documentedFileMembers[FMHL_EnumValues]++;
      }
      else if (md->isDefine())
      {
        g_fileIndexLetterUsed[FMHL_Defines][letter].append(md);
        documentedFileMembers[FMHL_Defines]++;
      }
    }
  }
}

//----------------------------------------------------------------------------

void writeQuickMemberIndex(OutputList &ol,
    MemberIndexList charUsed[MEMBER_INDEX_ENTRIES],int page,
    QCString fullName,bool multiPage)
{
  bool first=TRUE;
  int i;
  startQuickIndexList(ol,TRUE);
  for (i=33;i<127;i++)
  {
    char is[2];is[0]=(char)i;is[1]='\0';
    QCString ci = letterToLabel((char)i);
    if (charUsed[i].count()>0)
    {
      QCString anchor;
      QCString extension=Doxygen::htmlFileExtension;
      if (!multiPage)
        anchor="#index_";
      else if (first) 
        anchor=fullName+extension+"#index_";
      else 
        anchor=fullName+QCString().sprintf("_0x%02x",i)+extension+"#index_";
      startQuickIndexItem(ol,anchor+ci,i==page,TRUE,first);
      ol.writeString(is);
      endQuickIndexItem(ol);
      first=FALSE;
    }
  }
  endQuickIndexList(ol);
}

//----------------------------------------------------------------------------

struct CmhlInfo
{
  CmhlInfo(const char *fn,const char *t) : fname(fn), title(t) {}
  const char *fname;
  QCString title;
};

static const CmhlInfo *getCmhlInfo(int hl)
{
  static bool fortranOpt = Config_getBool("OPTIMIZE_FOR_FORTRAN");
  static bool vhdlOpt    = Config_getBool("OPTIMIZE_OUTPUT_VHDL");
  static CmhlInfo cmhlInfo[] = 
  {
    CmhlInfo("functions",     theTranslator->trAll()),
    CmhlInfo("functions_func",
        fortranOpt ? theTranslator->trSubprograms() : 
        vhdlOpt    ? VhdlDocGen::trFunctionAndProc() :
                     theTranslator->trFunctions()),
    CmhlInfo("functions_vars",theTranslator->trVariables()),
    CmhlInfo("functions_type",theTranslator->trTypedefs()),
    CmhlInfo("functions_enum",theTranslator->trEnumerations()),
    CmhlInfo("functions_eval",theTranslator->trEnumerationValues()),
    CmhlInfo("functions_prop",theTranslator->trProperties()),
    CmhlInfo("functions_evnt",theTranslator->trEvents()),
    CmhlInfo("functions_rela",theTranslator->trRelatedFunctions())
  };
  return &cmhlInfo[hl];
}

static void writeClassMemberIndexFiltered(OutputList &ol, ClassMemberHighlight hl)
{
  if (documentedClassMembers[hl]==0) return;
  
  static bool generateTreeView = Config_getBool("GENERATE_TREEVIEW");
  static bool disableIndex     = Config_getBool("DISABLE_INDEX");

  bool multiPageIndex=FALSE;
  int numPages=1;
  if (documentedClassMembers[hl]>MAX_ITEMS_BEFORE_MULTIPAGE_INDEX)
  {
    multiPageIndex=TRUE;
    numPages=127;
  }

  ol.pushGeneratorState();
  ol.disableAllBut(OutputGenerator::Html);

  QCString extension=Doxygen::htmlFileExtension;
  LayoutNavEntry *lne = LayoutDocManager::instance().rootNavEntry()->find(LayoutNavEntry::ClassMembers);
  QCString title = lne->title();
  if (hl!=CMHL_All) title+=(QCString)" - "+getCmhlInfo(hl)->title;

  int page;
  bool first=TRUE;
  for (page=0;page<numPages;page++)
  {
    if (!multiPageIndex || g_memberIndexLetterUsed[hl][page].count()>0)
    {
      QCString fileName = getCmhlInfo(hl)->fname;
      if (multiPageIndex && !first)
      { 
        fileName+=QCString().sprintf("_0x%02x",page);
      }
      bool quickIndex = documentedClassMembers[hl]>maxItemsBeforeQuickIndex;
      
      ol.startFile(fileName+extension,0,title);
      ol.startQuickIndices();
      if (!disableIndex)
      {
        ol.writeQuickLinks(TRUE,HLI_Functions);
      }
      startQuickIndexList(ol);

      // index item for global member list
      startQuickIndexItem(ol,
          getCmhlInfo(0)->fname+Doxygen::htmlFileExtension,hl==CMHL_All,TRUE,first);
      ol.writeString(fixSpaces(getCmhlInfo(0)->title));
      endQuickIndexItem(ol);

      // index items per category member lists
      int i;
      for (i=1;i<CMHL_Total;i++)
      {
        if (documentedClassMembers[i]>0)
        {
          startQuickIndexItem(ol,getCmhlInfo(i)->fname+Doxygen::htmlFileExtension,hl==i,TRUE,first);
          ol.writeString(fixSpaces(getCmhlInfo(i)->title));
          //printf("multiPageIndex=%d first=%d fileName=%s file=%s title=%s\n",
          //    multiPageIndex,first,fileName.data(),getCmhlInfo(i)->fname,getCmhlInfo(i)->title.data());
          endQuickIndexItem(ol);
        }
      }

      endQuickIndexList(ol);

      // quick alphabetical index
      if (quickIndex)
      {
        writeQuickMemberIndex(ol,g_memberIndexLetterUsed[hl],page,
            getCmhlInfo(hl)->fname,multiPageIndex);
      }
      ol.endQuickIndices();

      if (generateTreeView)
      {
        ol.writeSplitBar(getCmhlInfo(0)->fname);
      }

      ol.startContents();

      if (hl==CMHL_All)
      {
        ol.startTextBlock();
        ol.parseText(lne->intro());
        ol.endTextBlock();
      }
      else
      {
        // hack to work around a mozilla bug, which refuses to switch to
        // normal lists otherwise
        ol.writeString("&#160;");
      }
      //ol.newParagraph();  // FIXME:PARA
      writeMemberList(ol,quickIndex,
                      multiPageIndex?page:-1,
                      g_memberIndexLetterUsed[hl],
                      Definition::TypeClass);
      endFile(ol);
      first=FALSE;
    }
  }
  
  ol.popGeneratorState();
}

void writeClassMemberIndex(OutputList &ol)
{
  //bool fortranOpt = Config_getBool("OPTIMIZE_FOR_FORTRAN");
  writeClassMemberIndexFiltered(ol,CMHL_All);
  writeClassMemberIndexFiltered(ol,CMHL_Functions);
  writeClassMemberIndexFiltered(ol,CMHL_Variables);
  writeClassMemberIndexFiltered(ol,CMHL_Typedefs);
  writeClassMemberIndexFiltered(ol,CMHL_Enums);
  writeClassMemberIndexFiltered(ol,CMHL_EnumValues);
  writeClassMemberIndexFiltered(ol,CMHL_Properties);
  writeClassMemberIndexFiltered(ol,CMHL_Events);
  writeClassMemberIndexFiltered(ol,CMHL_Related);

  if (documentedClassMembers[CMHL_All]>0)
  {
    LayoutNavEntry *lne = LayoutDocManager::instance().rootNavEntry()->find(LayoutNavEntry::ClassMembers);
    Doxygen::indexList.addContentsItem(FALSE,lne->title(),0,"functions",0); 
  }
}

//----------------------------------------------------------------------------

struct FmhlInfo 
{
  FmhlInfo(const char *fn,const char *t) : fname(fn), title(t) {}
  const char *fname;
  QCString title;
};

static const FmhlInfo *getFmhlInfo(int hl)
{
  static bool fortranOpt = Config_getBool("OPTIMIZE_FOR_FORTRAN");
  static bool vhdlOpt    = Config_getBool("OPTIMIZE_OUTPUT_VHDL");
  static FmhlInfo fmhlInfo[] = 
  {
    FmhlInfo("globals",     theTranslator->trAll()),
    FmhlInfo("globals_func",
         fortranOpt ? theTranslator->trSubprograms()  : 
         vhdlOpt    ? VhdlDocGen::trFunctionAndProc() : 
                      theTranslator->trFunctions()),
    FmhlInfo("globals_vars",theTranslator->trVariables()),
    FmhlInfo("globals_type",theTranslator->trTypedefs()),
    FmhlInfo("globals_enum",theTranslator->trEnumerations()),
    FmhlInfo("globals_eval",theTranslator->trEnumerationValues()),
    FmhlInfo("globals_defs",theTranslator->trDefines())
  };
  return &fmhlInfo[hl];
}

static void writeFileMemberIndexFiltered(OutputList &ol, FileMemberHighlight hl)
{
  if (documentedFileMembers[hl]==0) return;

  static bool generateTreeView = Config_getBool("GENERATE_TREEVIEW");
  //static bool fortranOpt       = Config_getBool("OPTIMIZE_FOR_FORTRAN");
  static bool disableIndex     = Config_getBool("DISABLE_INDEX");

  bool multiPageIndex=FALSE;
  int numPages=1;
  if (documentedFileMembers[hl]>MAX_ITEMS_BEFORE_MULTIPAGE_INDEX)
  {
    multiPageIndex=TRUE;
    numPages=127;
  }

  ol.pushGeneratorState();
  ol.disableAllBut(OutputGenerator::Html);

  QCString extension=Doxygen::htmlFileExtension;
  LayoutNavEntry *lne = LayoutDocManager::instance().rootNavEntry()->find(LayoutNavEntry::FileGlobals);
  QCString title = lne->title();

  int page;
  bool first=TRUE;
  for (page=0;page<numPages;page++)
  {
    if (!multiPageIndex || g_fileIndexLetterUsed[hl][page].count()>0)
    {
      QCString fileName = getFmhlInfo(hl)->fname;
      if (multiPageIndex && !first)
      {
        fileName+=QCString().sprintf("_0x%02x",page);
      }
      bool quickIndex = documentedFileMembers[hl]>maxItemsBeforeQuickIndex;
      
      ol.startFile(fileName+extension,0,title);
      ol.startQuickIndices();
      if (!disableIndex)
      {
        ol.writeQuickLinks(TRUE,HLI_Globals);
      }
      startQuickIndexList(ol);

      // index item for all member lists
      startQuickIndexItem(ol,
          getFmhlInfo(0)->fname+Doxygen::htmlFileExtension,hl==FMHL_All,TRUE,first);
      ol.writeString(fixSpaces(getFmhlInfo(0)->title));
      endQuickIndexItem(ol);

      int i;
      // index items for per category member lists
      for (i=1;i<FMHL_Total;i++)
      {
        if (documentedFileMembers[i]>0)
        {
          startQuickIndexItem(ol,
              getFmhlInfo(i)->fname+Doxygen::htmlFileExtension,hl==i,TRUE,first);
          ol.writeString(fixSpaces(getFmhlInfo(i)->title));
          endQuickIndexItem(ol);
        }
      }

      endQuickIndexList(ol);

      if (quickIndex)
      {
        writeQuickMemberIndex(ol,g_fileIndexLetterUsed[hl],page,
            getFmhlInfo(hl)->fname,multiPageIndex);
      }
      ol.endQuickIndices();

      if (generateTreeView)
      {
        ol.writeSplitBar(getFmhlInfo(0)->fname);
      }

      ol.startContents();

      if (hl==FMHL_All)
      {
        ol.startTextBlock();
        ol.parseText(lne->intro());
        ol.endTextBlock();
      }
      else
      {
        // hack to work around a mozilla bug, which refuses to switch to
        // normal lists otherwise
        ol.writeString("&#160;");
      }
      //ol.newParagraph();  // FIXME:PARA
      //writeFileMemberList(ol,quickIndex,hl,page);
      writeMemberList(ol,quickIndex,
          multiPageIndex?page:-1,
          g_fileIndexLetterUsed[hl],
          Definition::TypeFile);
      endFile(ol);
      first=FALSE;
    }
  }
  ol.popGeneratorState();
}

void writeFileMemberIndex(OutputList &ol)
{
  writeFileMemberIndexFiltered(ol,FMHL_All);
  writeFileMemberIndexFiltered(ol,FMHL_Functions);
  writeFileMemberIndexFiltered(ol,FMHL_Variables);
  writeFileMemberIndexFiltered(ol,FMHL_Typedefs);
  writeFileMemberIndexFiltered(ol,FMHL_Enums);
  writeFileMemberIndexFiltered(ol,FMHL_EnumValues);
  writeFileMemberIndexFiltered(ol,FMHL_Defines);

  if (documentedFileMembers[FMHL_All]>0)
  {
    LayoutNavEntry *lne = LayoutDocManager::instance().rootNavEntry()->find(LayoutNavEntry::FileGlobals);
    Doxygen::indexList.addContentsItem(FALSE,lne->title(),0,"globals",0); 
  }
}

//----------------------------------------------------------------------------

struct NmhlInfo
{
  NmhlInfo(const char *fn,const char *t) : fname(fn), title(t) {}
  const char *fname;
  QCString title;
};

static const NmhlInfo *getNmhlInfo(int hl)
{
  static bool fortranOpt = Config_getBool("OPTIMIZE_FOR_FORTRAN");
  static bool vhdlOpt    = Config_getBool("OPTIMIZE_OUTPUT_VHDL");
  static NmhlInfo nmhlInfo[] = 
  {
    NmhlInfo("namespacemembers",     theTranslator->trAll()),
    NmhlInfo("namespacemembers_func",
        fortranOpt ? theTranslator->trSubprograms()  :
        vhdlOpt    ? VhdlDocGen::trFunctionAndProc() :
                     theTranslator->trFunctions()),
    NmhlInfo("namespacemembers_vars",theTranslator->trVariables()),
    NmhlInfo("namespacemembers_type",theTranslator->trTypedefs()),
    NmhlInfo("namespacemembers_enum",theTranslator->trEnumerations()),
    NmhlInfo("namespacemembers_eval",theTranslator->trEnumerationValues())
  };
  return &nmhlInfo[hl];
}

//----------------------------------------------------------------------------

static void writeNamespaceMemberIndexFiltered(OutputList &ol,
                                        NamespaceMemberHighlight hl)
{
  if (documentedNamespaceMembers[hl]==0) return;

  static bool generateTreeView = Config_getBool("GENERATE_TREEVIEW");
  static bool disableIndex     = Config_getBool("DISABLE_INDEX");

  bool multiPageIndex=FALSE;
  int numPages=1;
  if (documentedNamespaceMembers[hl]>MAX_ITEMS_BEFORE_MULTIPAGE_INDEX)
  {
    multiPageIndex=TRUE;
    numPages=127;
  }

  ol.pushGeneratorState();
  ol.disableAllBut(OutputGenerator::Html);

  QCString extension=Doxygen::htmlFileExtension;
  LayoutNavEntry *lne = LayoutDocManager::instance().rootNavEntry()->find(LayoutNavEntry::NamespaceMembers);
  QCString title = lne->title();

  int page;
  bool first=TRUE;
  for (page=0;page<numPages;page++)
  {
    if (!multiPageIndex || g_namespaceIndexLetterUsed[hl][page].count()>0)
    {
      QCString fileName = getNmhlInfo(hl)->fname;
      if (multiPageIndex && !first)
      {
        fileName+=QCString().sprintf("_0x%02x",page);
      }
      bool quickIndex = documentedNamespaceMembers[hl]>maxItemsBeforeQuickIndex;
      
      ol.startFile(fileName+extension,0,title);
      ol.startQuickIndices();
      if (!disableIndex)
      {
        ol.writeQuickLinks(TRUE,HLI_NamespaceMembers);
      }
      startQuickIndexList(ol);

      startQuickIndexItem(ol,
          getNmhlInfo(0)->fname+Doxygen::htmlFileExtension,hl==NMHL_All,TRUE,first);
      ol.writeString(fixSpaces(getNmhlInfo(0)->title));
      endQuickIndexItem(ol);

      int i;
      for (i=1;i<NMHL_Total;i++)
      {
        if (documentedNamespaceMembers[i]>0)
        {
          startQuickIndexItem(ol,
              getNmhlInfo(i)->fname+Doxygen::htmlFileExtension,hl==i,TRUE,first);
          ol.writeString(fixSpaces(getNmhlInfo(i)->title));
          endQuickIndexItem(ol);
        }
      }

      endQuickIndexList(ol);

      if (quickIndex)
      {
        writeQuickMemberIndex(ol,g_namespaceIndexLetterUsed[hl],page,
            getNmhlInfo(hl)->fname,multiPageIndex);
      }

      ol.endQuickIndices();

      if (generateTreeView)
      {
        ol.writeSplitBar(getNmhlInfo(0)->fname);
      }

      ol.startContents();

      if (hl==NMHL_All)
      {
        ol.startTextBlock();
        ol.parseText(lne->intro());
        ol.endTextBlock();
      }
      else
      {
        // hack to work around a mozilla bug, which refuses to switch to
        // normal lists otherwise
        ol.writeString("&#160;");
      }
      //ol.newParagraph(); // FIXME:PARA

      //writeNamespaceMemberList(ol,quickIndex,hl,page);
      writeMemberList(ol,quickIndex,
                      multiPageIndex?page:-1,
                      g_namespaceIndexLetterUsed[hl],
                      Definition::TypeNamespace);
      endFile(ol);
    }
  }
  ol.popGeneratorState();
}

void writeNamespaceMemberIndex(OutputList &ol)
{
  //bool fortranOpt = Config_getBool("OPTIMIZE_FOR_FORTRAN");
  writeNamespaceMemberIndexFiltered(ol,NMHL_All);
  writeNamespaceMemberIndexFiltered(ol,NMHL_Functions);
  writeNamespaceMemberIndexFiltered(ol,NMHL_Variables);
  writeNamespaceMemberIndexFiltered(ol,NMHL_Typedefs);
  writeNamespaceMemberIndexFiltered(ol,NMHL_Enums);
  writeNamespaceMemberIndexFiltered(ol,NMHL_EnumValues);

  if (documentedNamespaceMembers[NMHL_All]>0)
  {
    LayoutNavEntry *lne = LayoutDocManager::instance().rootNavEntry()->find(LayoutNavEntry::NamespaceMembers);
    Doxygen::indexList.addContentsItem(FALSE,lne->title(),0,"namespacemembers",0); 
  }
}

//----------------------------------------------------------------------------

#define NUM_SEARCH_INDICES      13
#define SEARCH_INDEX_ALL         0
#define SEARCH_INDEX_CLASSES     1
#define SEARCH_INDEX_NAMESPACES  2
#define SEARCH_INDEX_FILES       3
#define SEARCH_INDEX_FUNCTIONS   4
#define SEARCH_INDEX_VARIABLES   5
#define SEARCH_INDEX_TYPEDEFS    6
#define SEARCH_INDEX_ENUMS       7
#define SEARCH_INDEX_ENUMVALUES  8
#define SEARCH_INDEX_PROPERTIES  9
#define SEARCH_INDEX_EVENTS     10
#define SEARCH_INDEX_RELATED    11
#define SEARCH_INDEX_DEFINES    12

class SearchIndexList : public SDict< QList<Definition> >
{
  public:
    SearchIndexList(int size=17) : SDict< QList<Definition> >(size,FALSE) 
    {
      setAutoDelete(TRUE);
    }
   ~SearchIndexList() {}
    void append(Definition *d)
    {
      QList<Definition> *l = find(d->name());
      if (l==0)
      {
        l=new QList<Definition>;
        SDict< QList<Definition> >::append(d->name(),l);
      }
      l->append(d);
    }
    int compareItems(GCI item1, GCI item2)
    {
      QList<Definition> *md1=(QList<Definition> *)item1;
      QList<Definition> *md2=(QList<Definition> *)item2;
      QCString n1 = md1->first()->localName();
      QCString n2 = md2->first()->localName();
      return stricmp(n1.data(),n2.data());
    }
};

static void addMemberToSearchIndex(
         SearchIndexList symbols[NUM_SEARCH_INDICES][MEMBER_INDEX_ENTRIES],
         int symbolCount[NUM_SEARCH_INDICES],
         MemberDef *md)
{
  static bool hideFriendCompounds = Config_getBool("HIDE_FRIEND_COMPOUNDS");
  bool isLinkable = md->isLinkable();
  ClassDef *cd=0;
  NamespaceDef *nd=0;
  FileDef *fd=0;
  if (isLinkable             && 
      (cd=md->getClassDef()) && 
      cd->isLinkable()       &&
      cd->templateMaster()==0)
  {
    QCString n = md->name();
    uchar charCode = (uchar)n.at(0);
    uint letter = charCode<128 ? tolower(charCode) : charCode;
    if (!n.isEmpty()) 
    {
      bool isFriendToHide = hideFriendCompounds &&
        (QCString(md->typeString())=="friend class" || 
         QCString(md->typeString())=="friend struct" ||
         QCString(md->typeString())=="friend union");
      if (!(md->isFriend() && isFriendToHide))
      {
        symbols[SEARCH_INDEX_ALL][letter].append(md);
        symbolCount[SEARCH_INDEX_ALL]++;
      }
      if (md->isFunction() || md->isSlot() || md->isSignal())
      {
        symbols[SEARCH_INDEX_FUNCTIONS][letter].append(md);
        symbolCount[SEARCH_INDEX_FUNCTIONS]++;
      } 
      else if (md->isVariable())
      {
        symbols[SEARCH_INDEX_VARIABLES][letter].append(md);
        symbolCount[SEARCH_INDEX_VARIABLES]++;
      }
      else if (md->isTypedef())
      {
        symbols[SEARCH_INDEX_TYPEDEFS][letter].append(md);
        symbolCount[SEARCH_INDEX_TYPEDEFS]++;
      }
      else if (md->isEnumerate())
      {
        symbols[SEARCH_INDEX_ENUMS][letter].append(md);
        symbolCount[SEARCH_INDEX_ENUMS]++;
      }
      else if (md->isEnumValue())
      {
        symbols[SEARCH_INDEX_ENUMVALUES][letter].append(md);
        symbolCount[SEARCH_INDEX_ENUMVALUES]++;
      }
      else if (md->isProperty())
      {
        symbols[SEARCH_INDEX_PROPERTIES][letter].append(md);
        symbolCount[SEARCH_INDEX_PROPERTIES]++;
      }
      else if (md->isEvent())
      {
        symbols[SEARCH_INDEX_EVENTS][letter].append(md);
        symbolCount[SEARCH_INDEX_EVENTS]++;
      }
      else if (md->isRelated() || md->isForeign() ||
               (md->isFriend() && !isFriendToHide))
      {
        symbols[SEARCH_INDEX_RELATED][letter].append(md);
        symbolCount[SEARCH_INDEX_RELATED]++;
      }
    }
  }
  else if (isLinkable && 
      (((nd=md->getNamespaceDef()) && nd->isLinkable()) || 
       ((fd=md->getFileDef())      && fd->isLinkable())
      )
     )
  {
    QCString n = md->name();
    uchar charCode = (uchar)n.at(0);
    uint letter = charCode<128 ? tolower(charCode) : charCode;
    if (!n.isEmpty()) 
    {
      symbols[SEARCH_INDEX_ALL][letter].append(md);
      symbolCount[SEARCH_INDEX_ALL]++;

      if (md->isFunction()) 
      {
        symbols[SEARCH_INDEX_FUNCTIONS][letter].append(md);
        symbolCount[SEARCH_INDEX_FUNCTIONS]++;
      }
      else if (md->isVariable()) 
      {
        symbols[SEARCH_INDEX_VARIABLES][letter].append(md);
        symbolCount[SEARCH_INDEX_VARIABLES]++;
      }
      else if (md->isTypedef())
      {
        symbols[SEARCH_INDEX_TYPEDEFS][letter].append(md);
        symbolCount[SEARCH_INDEX_TYPEDEFS]++;
      }
      else if (md->isEnumerate())
      {
        symbols[SEARCH_INDEX_ENUMS][letter].append(md);
        symbolCount[SEARCH_INDEX_ENUMS]++;
      }
      else if (md->isEnumValue())
      {
        symbols[SEARCH_INDEX_ENUMVALUES][letter].append(md);
        symbolCount[SEARCH_INDEX_ENUMVALUES]++;
      }
      else if (md->isDefine())
      {
        symbols[SEARCH_INDEX_DEFINES][letter].append(md);
        symbolCount[SEARCH_INDEX_DEFINES]++;
      }
    }
  }
}

static QCString searchId(const QCString &s)
{
  int c;
  uint i;
  QCString result;
  for (i=0;i<s.length();i++)
  {
    c=s.at(i);
    if ((c>='0' && c<='9') || (c>='A' && c<='Z') || (c>='a' && c<='z'))
    {
      result+=(char)tolower(c);
    }
    else
    {
      char val[4];
      sprintf(val,"_%02x",(uchar)c);
      result+=val;
    }
  }
  return result;
}

static  int g_searchIndexCount[NUM_SEARCH_INDICES];
static  SearchIndexList g_searchIndexSymbols[NUM_SEARCH_INDICES][MEMBER_INDEX_ENTRIES];
static const char *g_searchIndexName[NUM_SEARCH_INDICES] = 
{ 
    "all",
    "classes",
    "namespaces",
    "files",
    "functions",
    "variables",
    "typedefs", 
    "enums", 
    "enumvalues",
    "properties", 
    "events", 
    "related",
    "defines"
};


class SearchIndexCategoryMapping
{
  public:
    SearchIndexCategoryMapping()
    {
      categoryLabel[SEARCH_INDEX_ALL]        = theTranslator->trAll();
      categoryLabel[SEARCH_INDEX_CLASSES]    = theTranslator->trClasses();
      categoryLabel[SEARCH_INDEX_NAMESPACES] = theTranslator->trNamespace(TRUE,FALSE);
      categoryLabel[SEARCH_INDEX_FILES]      = theTranslator->trFile(TRUE,FALSE);
      categoryLabel[SEARCH_INDEX_FUNCTIONS]  = theTranslator->trFunctions();
      categoryLabel[SEARCH_INDEX_VARIABLES]  = theTranslator->trVariables();
      categoryLabel[SEARCH_INDEX_TYPEDEFS]   = theTranslator->trTypedefs();
      categoryLabel[SEARCH_INDEX_ENUMS]      = theTranslator->trEnumerations();
      categoryLabel[SEARCH_INDEX_ENUMVALUES] = theTranslator->trEnumerationValues();
      categoryLabel[SEARCH_INDEX_PROPERTIES] = theTranslator->trProperties();
      categoryLabel[SEARCH_INDEX_EVENTS]     = theTranslator->trEvents();
      categoryLabel[SEARCH_INDEX_RELATED]    = theTranslator->trFriends();
      categoryLabel[SEARCH_INDEX_DEFINES]    = theTranslator->trDefines();
    }
    QCString categoryLabel[NUM_SEARCH_INDICES];
};

void writeJavascriptSearchIndex()
{
  if (!Config_getBool("GENERATE_HTML")) return;
  //static bool treeView = Config_getBool("GENERATE_TREEVIEW");

  ClassSDict::Iterator cli(*Doxygen::classSDict);
  ClassDef *cd;
  for (;(cd=cli.current());++cli)
  {
    uchar charCode = (uchar)cd->localName().at(0);
    uint letter = charCode<128 ? tolower(charCode) : charCode;
    if (cd->isLinkable() && isId(letter))
    {
      g_searchIndexSymbols[SEARCH_INDEX_ALL][letter].append(cd);
      g_searchIndexSymbols[SEARCH_INDEX_CLASSES][letter].append(cd);
      g_searchIndexCount[SEARCH_INDEX_ALL]++;
      g_searchIndexCount[SEARCH_INDEX_CLASSES]++;
    }
  }
  NamespaceSDict::Iterator nli(*Doxygen::namespaceSDict);
  NamespaceDef *nd;
  for (;(nd=nli.current());++nli)
  {
    uchar charCode = (uchar)nd->name().at(0);
    uint letter = charCode<128 ? tolower(charCode) : charCode;
    if (nd->isLinkable() && isId(letter))
    {
      g_searchIndexSymbols[SEARCH_INDEX_ALL][letter].append(nd);
      g_searchIndexSymbols[SEARCH_INDEX_NAMESPACES][letter].append(nd);
      g_searchIndexCount[SEARCH_INDEX_ALL]++;
      g_searchIndexCount[SEARCH_INDEX_NAMESPACES]++;
    }
  }
  FileNameListIterator fnli(*Doxygen::inputNameList);
  FileName *fn;
  for (;(fn=fnli.current());++fnli)
  {
    FileNameIterator fni(*fn);
    FileDef *fd;
    for (;(fd=fni.current());++fni)
    {
      uchar charCode = (uchar)fd->name().at(0);
      uint letter = charCode<128 ? tolower(charCode) : charCode;
      if (fd->isLinkable() && isId(letter))
      {
        g_searchIndexSymbols[SEARCH_INDEX_ALL][letter].append(fd);
        g_searchIndexSymbols[SEARCH_INDEX_FILES][letter].append(fd);
        g_searchIndexCount[SEARCH_INDEX_ALL]++;
        g_searchIndexCount[SEARCH_INDEX_FILES]++;
      }
    }
  }
  {
    MemberNameSDict::Iterator mnli(*Doxygen::memberNameSDict);
    MemberName *mn;
    // for each member name
    for (mnli.toFirst();(mn=mnli.current());++mnli)
    {
      MemberDef *md;
      MemberNameIterator mni(*mn);
      // for each member definition
      for (mni.toFirst();(md=mni.current());++mni)
      {
        addMemberToSearchIndex(g_searchIndexSymbols,g_searchIndexCount,md);
      }
    }
  }
  {
    MemberNameSDict::Iterator fnli(*Doxygen::functionNameSDict);
    MemberName *mn;
    // for each member name
    for (fnli.toFirst();(mn=fnli.current());++fnli)
    {
      MemberDef *md;
      MemberNameIterator mni(*mn);
      // for each member definition
      for (mni.toFirst();(md=mni.current());++mni)
      {
        addMemberToSearchIndex(g_searchIndexSymbols,g_searchIndexCount,md);
      }
    }
  }
  
  int i,p;
  for (i=0;i<NUM_SEARCH_INDICES;i++)
  {
    for (p=0;p<MEMBER_INDEX_ENTRIES;p++)
    {
      if (g_searchIndexSymbols[i][p].count()>0)
      {
        g_searchIndexSymbols[i][p].sort();
      }
    }
  }

  QCString searchDirName = Config_getString("HTML_OUTPUT")+"/search";

  for (i=0;i<NUM_SEARCH_INDICES;i++)
  {
    for (p=0;p<MEMBER_INDEX_ENTRIES;p++)
    {
      if (g_searchIndexSymbols[i][p].count()>0)
      {
        QCString fileName;
        fileName.sprintf("/%s_%02x.html",g_searchIndexName[i],p);
        fileName.prepend(searchDirName);
        QFile outFile(fileName);
        if (outFile.open(IO_WriteOnly))
        {
          FTextStream t(&outFile);
          t << "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\""
               " \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">" << endl;
          t << "<html><head><title></title>" << endl;
          t << "<meta http-equiv=\"Content-Type\" content=\"text/xhtml;charset=UTF-8\"/>" << endl;
          t << "<link rel=\"stylesheet\" type=\"text/css\" href=\"search.css\"/>" << endl;
          t << "<script type=\"text/javascript\" src=\"search.js\"></script>" << endl;
          t << "</head>" << endl;
          t << "<body class=\"SRPage\">" << endl;
          t << "<div id=\"SRIndex\">" << endl;
          t << "<div class=\"SRStatus\" id=\"Loading\">" << theTranslator->trLoading() << "</div>" << endl;

          SDict<QList<Definition> >::Iterator li(g_searchIndexSymbols[i][p]);
          QList<Definition> *dl;
          int itemCount=0;
          for (li.toFirst();(dl=li.current());++li)
          {
            Definition *d = dl->first();
            QCString id = d->localName();
            t << "<div class=\"SRResult\" id=\"SR_"
              << searchId(d->localName()) << "\">" << endl;
            t << " <div class=\"SREntry\">\n";
            if (dl->count()==1) // item with a unique name
            {
              MemberDef  *md   = 0;
              bool isMemberDef = d->definitionType()==Definition::TypeMember;
              if (isMemberDef) md = (MemberDef*)d;
              t << "  <a id=\"Item" << itemCount << "\" "
                << "onkeydown=\""
                << "return searchResults.Nav(event," << itemCount << ")\" "
                << "onkeypress=\""
                << "return searchResults.Nav(event," << itemCount << ")\" "
                << "onkeyup=\""
                << "return searchResults.Nav(event," << itemCount << ")\" "
                << "class=\"SRSymbol\" ";
              t << externalLinkTarget() << "href=\"" << externalRef("../",d->getReference(),TRUE);
              t << d->getOutputFileBase() << Doxygen::htmlFileExtension;
              if (md)
              {
                t << "#" << md->anchor();
              }
              t << "\"";
              static bool extLinksInWindow = Config_getBool("EXT_LINKS_IN_WINDOW");
              if (!extLinksInWindow || d->getReference().isEmpty())
              {
                t << " target=\""; 
                /*if (treeView) t << "basefrm"; else*/ t << "_parent"; 
                t << "\"";
              }
              t << ">";
              t << convertToXML(d->localName());
              t << "</a>" << endl;
              if (d->getOuterScope()!=Doxygen::globalScope)
              {
                t << "  <span class=\"SRScope\">" 
                  << convertToXML(d->getOuterScope()->name()) 
                  << "</span>" << endl;
              }
              else if (md)
              {
                FileDef *fd = md->getBodyDef();
                if (fd==0) fd = md->getFileDef();
                if (fd)
                {
                  t << "  <span class=\"SRScope\">" 
                    << convertToXML(fd->localName())
                    << "</span>" << endl;
                }
              }
            }
            else // multiple items with the same name
            {
              t << "  <a id=\"Item" << itemCount << "\" "
                << "onkeydown=\""
                << "return searchResults.Nav(event," << itemCount << ")\" "
                << "onkeypress=\""
                << "return searchResults.Nav(event," << itemCount << ")\" "
                << "onkeyup=\""
                << "return searchResults.Nav(event," << itemCount << ")\" "
                << "class=\"SRSymbol\" "
                << "href=\"javascript:searchResults.Toggle('SR_"
                << searchId(d->localName()) << "')\">" 
                << convertToXML(d->localName()) << "</a>" << endl;
              t << "  <div class=\"SRChildren\">" << endl;

              QListIterator<Definition> di(*dl);
              bool overloadedFunction = FALSE;
              Definition *prevScope = 0;
              int childCount=0;
              for (di.toFirst();(d=di.current());)
              {
                ++di;
                Definition *scope     = d->getOuterScope();
                Definition *next      = di.current();
                Definition *nextScope = 0;
                MemberDef  *md        = 0;
                bool isMemberDef = d->definitionType()==Definition::TypeMember;
                if (isMemberDef) md = (MemberDef*)d;
                if (next) nextScope = next->getOuterScope();

                t << "    <a id=\"Item" << itemCount << "_c" 
                  << childCount << "\" "
                  << "onkeydown=\""
                  << "return searchResults.NavChild(event," 
                  << itemCount << "," << childCount << ")\" "
                  << "onkeypress=\""
                  << "return searchResults.NavChild(event," 
                  << itemCount << "," << childCount << ")\" "
                  << "onkeyup=\""
                  << "return searchResults.NavChild(event," 
                  << itemCount << "," << childCount << ")\" "
                  << "class=\"SRScope\" ";
                if (!d->getReference().isEmpty())
                {
                  t << externalLinkTarget() << externalRef("../",d->getReference(),FALSE);
                }
                t << "href=\"" << externalRef("../",d->getReference(),TRUE);
                t << d->getOutputFileBase() << Doxygen::htmlFileExtension;
                if (isMemberDef)
                {
                  t << "#" << ((MemberDef *)d)->anchor();
                }
                t << "\"";
                static bool extLinksInWindow = Config_getBool("EXT_LINKS_IN_WINDOW");
                if (!extLinksInWindow || d->getReference().isEmpty())
                {
                  t << " target=\"";
                  /*if (treeView) t << "basefrm"; else*/ t << "_parent"; 
                  t << "\"";
                }
                t << ">";
                bool found=FALSE;
                overloadedFunction = ((prevScope!=0 && scope==prevScope) ||
                                      (scope && scope==nextScope)
                                     ) && md && 
                                     (md->isFunction() || md->isSlot());
                QCString prefix;
                if (md) prefix=convertToXML(md->localName());
                if (overloadedFunction) // overloaded member function
                {
                  prefix+=convertToXML(md->argsString()); 
                          // show argument list to disambiguate overloaded functions
                }
                else if (md) // unique member function
                {
                  prefix+="()"; // only to show it is a function
                }
                if (d->definitionType()==Definition::TypeClass)
                {
                  t << convertToXML(((ClassDef*)d)->displayName());
                  found = TRUE;
                }
                else if (d->definitionType()==Definition::TypeNamespace)
                {
                  t << convertToXML(((NamespaceDef*)d)->displayName());
                  found = TRUE;
                }
                else if (scope==0 || scope==Doxygen::globalScope) // in global scope
                {
                  if (md)
                  {
                    FileDef *fd = md->getBodyDef();
                    if (fd==0) fd = md->getFileDef();
                    if (fd)
                    {
                      if (!prefix.isEmpty()) prefix+=":&#160;";
                      t << prefix << convertToXML(fd->localName());
                      found = TRUE;
                    }
                  }
                }
                else if (md && (md->getClassDef() || md->getNamespaceDef())) 
                  // member in class or namespace scope
                {
                  static bool optimizeOutputJava = Config_getBool("OPTIMIZE_OUTPUT_JAVA");
                  t << convertToXML(d->getOuterScope()->qualifiedName()) << (optimizeOutputJava ? "." : "::");
                  t << prefix;
                  found = TRUE;
                }
                else if (scope) // some thing else? -> show scope
                {
                  t << prefix << convertToXML(scope->name());
                  found = TRUE;
                }
                if (!found) // fallback
                {
                  t << prefix << "("+theTranslator->trGlobalNamespace()+")";
                }
                t << "</a>" << endl;
                prevScope = scope;
                childCount++;
              }
              t << "  </div>" << endl; // SRChildren
            }
            t << " </div>" << endl; // SREntry
            t << "</div>" << endl; // SRResult
            itemCount++;
          }
          t << "<div class=\"SRStatus\" id=\"Searching\">" 
            << theTranslator->trSearching() << "</div>" << endl;
          t << "<div class=\"SRStatus\" id=\"NoMatches\">"
            << theTranslator->trNoMatches() << "</div>" << endl;

          t << "<script type=\"text/javascript\"><!--" << endl;
          t << "document.getElementById(\"Loading\").style.display=\"none\";" << endl;
          t << "document.getElementById(\"NoMatches\").style.display=\"none\";" << endl;
          t << "var searchResults = new SearchResults(\"searchResults\");" << endl;
          t << "searchResults.Search();" << endl;
          t << "--></script>" << endl;

          t << "</div>" << endl; // SRIndex

          t << "</body>" << endl;
          t << "</html>" << endl;

        }
        else
        {
          err("Failed to open file '%s' for writing...\n",fileName.data());
        }
      }
    }
  }
  //ol.popGeneratorState();

  {
    QFile f(searchDirName+"/search.js");
    if (f.open(IO_WriteOnly))
    {
      FTextStream t(&f);
      t << "// Search script generated by doxygen" << endl;
      t << "// Copyright (C) 2009 by Dimitri van Heesch." << endl << endl;
      t << "// The code in this file is loosly based on main.js, part of Natural Docs," << endl;
      t << "// which is Copyright (C) 2003-2008 Greg Valure" << endl;
      t << "// Natural Docs is licensed under the GPL." << endl << endl;
      t << "var indexSectionsWithContent =" << endl;
      t << "{" << endl;
      bool first=TRUE;
      int j=0;
      for (i=0;i<NUM_SEARCH_INDICES;i++)
      {
        if (g_searchIndexCount[i]>0)
        {
          if (!first) t << "," << endl;
          t << "  " << j << ": \"";
          for (p=0;p<MEMBER_INDEX_ENTRIES;p++)
          {
            t << (g_searchIndexSymbols[i][p].count()>0 ? "1" : "0");
          }
          t << "\"";
          first=FALSE;
          j++;
        }
      }
      if (!first) t << "\n";
      t << "};" << endl << endl;
      t << "var indexSectionNames =" << endl;
      t << "{" << endl;
      first=TRUE;
      j=0;
      for (i=0;i<NUM_SEARCH_INDICES;i++)
      {
        if (g_searchIndexCount[i]>0)
        {
          if (!first) t << "," << endl;
          t << "  " << j << ": \"" << g_searchIndexName[i] << "\"";
          first=FALSE;
          j++;
        }
      }
      if (!first) t << "\n";
      t << "};" << endl << endl;
      t << search_script;
    }
  }
  {
    QFile f(searchDirName+"/nomatches.html");
    if (f.open(IO_WriteOnly))
    {
      FTextStream t(&f);
      t << "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" "
           "\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">" << endl;
      t << "<html><head><title></title>" << endl;
      t << "<meta http-equiv=\"Content-Type\" content=\"text/xhtml;charset=UTF-8\"/>" << endl;
      t << "<link rel=\"stylesheet\" type=\"text/css\" href=\"search.css\"/>" << endl;
      t << "<script type=\"text/javascript\" src=\"search.js\"></script>" << endl;
      t << "</head>" << endl;
      t << "<body class=\"SRPage\">" << endl;
      t << "<div id=\"SRIndex\">" << endl;
      t << "<div class=\"SRStatus\" id=\"NoMatches\">"
        << theTranslator->trNoMatches() << "</div>" << endl;
      t << "</div>" << endl;
      t << "</body>" << endl;
      t << "</html>" << endl;
    }
  }
  Doxygen::indexList.addStyleSheetFile("search/search.js");
}

void writeSearchCategories(FTextStream &t)
{
  static SearchIndexCategoryMapping map;
  int i,j=0;
  for (i=0;i<NUM_SEARCH_INDICES;i++)
  {
    if (g_searchIndexCount[i]>0)
    {
      t << "<a class=\"SelectItem\" href=\"javascript:void(0)\" "
        << "onclick=\"searchBox.OnSelectItem(" << j << ")\">"
        << "<span class=\"SelectionMark\">&#160;</span>"
        << convertToXML(map.categoryLabel[i])
        << "</a>";
      j++;
    }
  }
}

//----------------------------------------------------------------------------

void writeExampleIndex(OutputList &ol)
{
  if (Doxygen::exampleSDict->count()==0) return;
  ol.pushGeneratorState();
  ol.disable(OutputGenerator::Man);
  LayoutNavEntry *lne = LayoutDocManager::instance().rootNavEntry()->find(LayoutNavEntry::Examples);
  QCString title = lne->title();
  startFile(ol,"examples",0,title,HLI_Examples);
  startTitle(ol,0);
  ol.parseText(title);
  endTitle(ol,0,0);
  ol.startContents();
  ol.startTextBlock();
  Doxygen::indexList.addContentsItem(TRUE,title,0,"examples",0); 
  Doxygen::indexList.incContentsDepth();
  ol.parseText(lne->intro());
  ol.endTextBlock();
  ol.startItemList();
  PageSDict::Iterator pdi(*Doxygen::exampleSDict);
  PageDef *pd=0;
  for (pdi.toFirst();(pd=pdi.current());++pdi)
  {
    ol.startItemListItem();
    QCString n=pd->getOutputFileBase();
    if (!pd->title().isEmpty())
    {
      ol.writeObjectLink(0,n,0,pd->title());
      Doxygen::indexList.addContentsItem(FALSE,filterTitle(pd->title()),pd->getReference(),n,0);
    }
    else
    {
      ol.writeObjectLink(0,n,0,pd->name());
      Doxygen::indexList.addContentsItem(FALSE,pd->name(),pd->getReference(),n,0);
    }
    ol.endItemListItem();
    ol.writeString("\n");
  }
  ol.endItemList();
  Doxygen::indexList.decContentsDepth();
  endFile(ol);
  ol.popGeneratorState();
}


//----------------------------------------------------------------------------

template<typename T>
bool writeMemberNavIndex(FTextStream &t,
                         int indent,
                         int n,
                         int documentedMembers[],
                         MemberIndexList indexLetterUsed[][MEMBER_INDEX_ENTRIES],
                         const T *(*getInfo)(int),
                         bool &first
                        )

{
  bool found=FALSE;
  QCString indentStr;
  indentStr.fill(' ',indent*2);
  // index items per category member lists
  int i;
  for (i=0;i<n;i++)
  {
    bool hasIndex       = documentedMembers[i]>0;
    bool quickIndex     = documentedMembers[i]>maxItemsBeforeQuickIndex;
    bool multiIndexPage = documentedMembers[i]>MAX_ITEMS_BEFORE_MULTIPAGE_INDEX;
    if (hasIndex)
    {
      // terminate previous entry
      if (!first) t << "," << endl;
      first = FALSE;

      // start entry
      if (!found)
      {
        t << "[" << endl;
      }
      found = TRUE;

      t << indentStr << "  [ ";
      t << "\"" << fixSpaces(getInfo(i)->title) << "\", ";
      t << "\"" << getInfo(i)->fname << Doxygen::htmlFileExtension << "\", ";
      bool firstPage=TRUE;
      if (quickIndex)
      {
        t << "[ " << endl;
        int j;
        for (j=33;j<127;j++)
        {
          if (indexLetterUsed[i][j].count()>0)
          {
            if (!firstPage) t << "," << endl;
            QCString fullName = getInfo(i)->fname;
            QCString extension = Doxygen::htmlFileExtension;
            QCString anchor;
            if (firstPage || !multiIndexPage) 
              anchor=fullName+extension+"#index_";
            else 
              anchor=fullName+QCString().sprintf("_0x%02x",j)+extension+"#index_";
            char is[2];is[0]=(char)j;is[1]='\0';
            QCString ci = letterToLabel((char)j);
            t << indentStr << "    [ ";
            t << "\"" << is << "\", ";
            t << "\"" << anchor << ci << "\", null ]";
            firstPage=FALSE;
          }
        }
        t << endl << indentStr << "  ] ]";
      }
      else
      {
        t << "null" << " ]";
      }
    }
  }
  return found;
}

//----------------------------------------------------------------------------

bool writeFullNavIndex(FTextStream &t, LayoutNavEntry *root,int indent,bool &first)
{
  static struct NavEntryCountMap 
  {
    LayoutNavEntry::Kind kind;
    bool hasItems;
  } navEntryCountMap[] =
  {
    { LayoutNavEntry::MainPage,         TRUE                                   },
    { LayoutNavEntry::Pages,            indexedPages>0                         },
    { LayoutNavEntry::Modules,          documentedGroups>0                     },
    { LayoutNavEntry::Namespaces,       documentedNamespaces>0                 },
    { LayoutNavEntry::NamespaceMembers, documentedNamespaceMembers[NMHL_All]>0 },
    { LayoutNavEntry::Classes,          annotatedClasses>0                     },
    { LayoutNavEntry::ClassAnnotated,   annotatedClasses>0                     },
    { LayoutNavEntry::ClassHierarchy,   hierarchyClasses>0                     },
    { LayoutNavEntry::ClassMembers,     documentedClassMembers[CMHL_All]>0     },
    { LayoutNavEntry::Files,            documentedFiles>0                      },
    { LayoutNavEntry::FileGlobals,      documentedFileMembers[FMHL_All]>0      },
    { LayoutNavEntry::Dirs,             documentedDirs>0                       },
    { LayoutNavEntry::Examples,         Doxygen::exampleSDict->count()>0       }
  };

  QCString indentStr;
  indentStr.fill(' ',indent*2);
  bool found=FALSE;
  if (root->children().count()>0)
  {
    QListIterator<LayoutNavEntry> li(root->children());
    LayoutNavEntry *entry;
    for (li.toFirst();(entry=li.current());++li)
    {
      if (navEntryCountMap[entry->kind()].hasItems && entry->visible())
      {
        // terminate previous entry
        if (!first) t << "," << endl;
        first = FALSE;

        // start entry
        if (!found)
        {
          t << "[" << endl;
        }
        found = TRUE;

        bool emptySection=TRUE;
        t << indentStr << "  [ ";
        t << "\"" << fixSpaces(entry->title()) << "\", ";
        t << "\"" << entry->baseFile() << Doxygen::htmlFileExtension << "\", ";

        // write children (if any)
        bool firstChild=TRUE;
        if (entry->kind()==LayoutNavEntry::ClassMembers)
        {
          emptySection = !writeMemberNavIndex(t,indent+1,CMHL_Total,documentedClassMembers,g_memberIndexLetterUsed,&getCmhlInfo,firstChild);
        }
        else if (entry->kind()==LayoutNavEntry::NamespaceMembers)
        {
          emptySection = !writeMemberNavIndex(t,indent+1,NMHL_Total,documentedNamespaceMembers,g_namespaceIndexLetterUsed,&getNmhlInfo,firstChild);
        }
        else if (entry->kind()==LayoutNavEntry::FileGlobals)
        {
          emptySection = !writeMemberNavIndex(t,indent+1,FMHL_Total,documentedFileMembers,g_fileIndexLetterUsed,&getFmhlInfo,firstChild);
        }
        else
        {
          emptySection = !writeFullNavIndex(t,entry,indent+1,firstChild);
        }
        // end entry
        if (emptySection) // entry without children
          t << "null ]";
        else // entry with children
          t << endl << indentStr << "  ] ]";
      }
    }
  }
  return found;
}

//----------------------------------------------------------------------------

void countRelatedPages(int &docPages,int &indexPages)
{
  docPages=indexPages=0;
  PageSDict::Iterator pdi(*Doxygen::pageSDict);
  PageDef *pd=0;
  for (pdi.toFirst();(pd=pdi.current());++pdi)
  {
    if ( pd->visibleInIndex())
    {
      indexPages++; 
    }
    if ( pd->documentedPage())
    {
      docPages++;
    }
  }
}

//----------------------------------------------------------------------------

static void writeSubPages(PageDef *pd)
{
  //printf("Write subpages(%s #=%d)\n",pd->name().data(),pd->getSubPages() ? pd->getSubPages()->count() : 0 );
  Doxygen::indexList.incContentsDepth();

  PageSDict *subPages = pd->getSubPages();
  if (subPages)
  {
    PageSDict::Iterator pi(*subPages);
    PageDef *subPage;
    for (pi.toFirst();(subPage=pi.current());++pi)
    {
      QCString pageTitle;

      if (subPage->title().isEmpty())
        pageTitle=subPage->name();
      else
        pageTitle=subPage->title();

      bool hasSubPages = subPage->hasSubPages();

      Doxygen::indexList.addContentsItem(hasSubPages,pageTitle,subPage->getReference(),subPage->getOutputFileBase(),0);
      writeSubPages(subPage);
    }
  }
  Doxygen::indexList.decContentsDepth();

}

void writePageIndex(OutputList &ol)
{
  if (indexedPages==0) return;
  ol.pushGeneratorState();
  ol.disableAllBut(OutputGenerator::Html);
  LayoutNavEntry *lne = LayoutDocManager::instance().rootNavEntry()->find(LayoutNavEntry::Pages);
  QCString title = lne->title();
  startFile(ol,"pages",0,title,HLI_Pages);
  startTitle(ol,0);
  //if (!Config_getString("PROJECT_NAME").isEmpty()) 
  //{
  //  title.prepend(Config_getString("PROJECT_NAME")+" ");
  //}
  ol.parseText(title);
  endTitle(ol,0,0);
  ol.startContents();
  ol.startTextBlock();
  Doxygen::indexList.addContentsItem(TRUE,title,0,"pages",0); 
  Doxygen::indexList.incContentsDepth();
  ol.parseText(lne->intro());
  ol.endTextBlock();
  startIndexHierarchy(ol,0);
  PageSDict::Iterator pdi(*Doxygen::pageSDict);
  PageDef *pd=0;
  for (pdi.toFirst();(pd=pdi.current());++pdi)
  {
    if ( pd->visibleInIndex())
    {
      QCString pageTitle;

      if (pd->title().isEmpty())
        pageTitle=pd->name();
      else
        pageTitle=pd->title();

      bool hasSubPages = pd->hasSubPages();

      ol.startIndexListItem();
      ol.startIndexItem(pd->getReference(),pd->getOutputFileBase());
      ol.parseText(pageTitle);
      ol.endIndexItem(pd->getReference(),pd->getOutputFileBase());
      if (pd->isReference()) 
      { 
        ol.startTypewriter(); 
        ol.docify(" [external]");
        ol.endTypewriter();
      }
      ol.writeString("\n");
      Doxygen::indexList.addContentsItem(hasSubPages,filterTitle(pageTitle),pd->getReference(),pd->getOutputFileBase(),0);
      writeSubPages(pd);
      ol.endIndexListItem();
    }
  }
  endIndexHierarchy(ol,0);
  Doxygen::indexList.decContentsDepth();
  endFile(ol);
  ol.popGeneratorState();
}

//----------------------------------------------------------------------------

int countGroups()
{
  int count=0;
  GroupSDict::Iterator gli(*Doxygen::groupSDict);
  GroupDef *gd;
  for (gli.toFirst();(gd=gli.current());++gli)
  {
    if (!gd->isReference())
    {
      gd->visited=FALSE;
      count++;
    }
  }
  return count;
}

//----------------------------------------------------------------------------

int countDirs()
{
  int count=0;
  SDict<DirDef>::Iterator dli(*Doxygen::directories);
  DirDef *dd;
  for (dli.toFirst();(dd=dli.current());++dli)
  {
    if (dd->isLinkableInProject())
    {
      dd->visited=FALSE;
      count++;
    }
  }
  return count;
}


//----------------------------------------------------------------------------

void writeGraphInfo(OutputList &ol)
{
  if (!Config_getBool("HAVE_DOT") || !Config_getBool("GENERATE_HTML")) return;
  ol.pushGeneratorState();
  ol.disableAllBut(OutputGenerator::Html);
  generateGraphLegend(Config_getString("HTML_OUTPUT"));
  startFile(ol,"graph_legend",0,theTranslator->trLegendTitle().data());
  startTitle(ol,0);
  ol.parseText(theTranslator->trLegendTitle());
  endTitle(ol,0,0);
  ol.startContents();
  bool &stripCommentsStateRef = Config_getBool("STRIP_CODE_COMMENTS");
  bool oldStripCommentsState = stripCommentsStateRef;
  // temporarily disable the stripping of comments for our own code example!
  stripCommentsStateRef = FALSE;
  QCString legendDocs = theTranslator->trLegendDocs();
  int s = legendDocs.find("<center>");
  int e = legendDocs.find("</center>");
  if (Config_getEnum("DOT_IMAGE_FORMAT")=="svg" && s!=-1 && e!=-1)
  {
    legendDocs = legendDocs.left(s+8) + "[!-- SVG 0 --]\n" + legendDocs.mid(e); 
    //printf("legendDocs=%s\n",legendDocs.data());
  }
  ol.parseDoc("graph_legend",1,0,0,legendDocs,FALSE,FALSE);
  stripCommentsStateRef = oldStripCommentsState;
  endFile(ol);
  ol.popGeneratorState();
}

void writeGroupIndexItem(GroupDef *gd,MemberList *ml,const QCString &title)
{
  if (ml && ml->count()>0)
  {
    bool first=TRUE;
    MemberDef *md=ml->first();
    while (md)
    {
      if (md->isDetailedSectionVisible(TRUE,FALSE))
      {
        if (first)
        {
          first=FALSE;
          Doxygen::indexList.addContentsItem(TRUE, convertToHtml(title,TRUE), gd->getReference(), gd->getOutputFileBase(), 0);
          Doxygen::indexList.incContentsDepth();
        }
        Doxygen::indexList.addContentsItem(FALSE,md->name(),md->getReference(),md->getOutputFileBase(),md->anchor()); 
      }
      md=ml->next();
    }

    if (!first)
    {
      Doxygen::indexList.decContentsDepth();
    }
  }
}

//----------------------------------------------------------------------------
/*!
 * write groups as hierarchical trees
 */
void writeGroupTreeNode(OutputList &ol, GroupDef *gd, int level, FTVHelp* ftv)
{
  bool fortranOpt = Config_getBool("OPTIMIZE_FOR_FORTRAN");
  bool vhdlOpt    = Config_getBool("OPTIMIZE_OUTPUT_VHDL");  
  if (level>20)
  {
    warn(gd->getDefFileName(),gd->getDefLine(),
        "warning: maximum nesting level exceeded for group %s: check for possible recursive group relation!\n",gd->name().data()
        );
    return;
  }

  /* Some groups should appear twice under different parent-groups.
   * That is why we should not check if it was visited 
   */
  if (/*!gd->visited &&*/ (!gd->isASubGroup() || level>0) &&
      (!gd->isReference() || Config_getBool("EXTERNAL_GROUPS")) // hide external groups by default
     )
  {
    //printf("gd->name()=%s #members=%d\n",gd->name().data(),gd->countMembers());
    // write group info
    bool hasSubGroups = gd->groupList->count()>0;
    bool hasSubPages = gd->pageDict->count()>0;
    int numSubItems = 0;
    if ( Config_getBool("TOC_EXPAND"))
    {
      QListIterator<MemberList> mli(gd->getMemberLists());
      MemberList *ml;
      for (mli.toFirst();(ml=mli.current());++mli)
      {
        if (ml->listType()&MemberList::documentationLists)
        {
          numSubItems += ml->count();
        }
      }
      numSubItems += gd->namespaceSDict->count();
      numSubItems += gd->classSDict->count();
      numSubItems += gd->fileList->count();
      numSubItems += gd->exampleDict->count();
    }

    bool isDir = hasSubGroups || hasSubPages || numSubItems>0;
    //printf("gd=`%s': pageDict=%d\n",gd->name().data(),gd->pageDict->count());
    Doxygen::indexList.addContentsItem(isDir,gd->groupTitle(),gd->getReference(),gd->getOutputFileBase(),0); 
    Doxygen::indexList.incContentsDepth();
    if (ftv)
    {
      ftv->addContentsItem(isDir,gd->groupTitle(),gd->getReference(),gd->getOutputFileBase(),0); 
      ftv->incContentsDepth();
    }
    
    //ol.writeListItem();
    //ol.startTextLink(gd->getOutputFileBase(),0);
    //parseText(ol,gd->groupTitle());
    //ol.endTextLink();

    ol.startIndexListItem();
    ol.startIndexItem(gd->getReference(),gd->getOutputFileBase());
    ol.parseText(gd->groupTitle());
    ol.endIndexItem(gd->getReference(),gd->getOutputFileBase());
    if (gd->isReference()) 
    { 
      ol.startTypewriter(); 
      ol.docify(" [external]");
      ol.endTypewriter();
    }
    
    
    // write pages
    PageSDict::Iterator pli(*gd->pageDict);
    PageDef *pd = 0;
    for (pli.toFirst();(pd=pli.current());++pli)
    {
      SectionInfo *si=0;
      if (!pd->name().isEmpty()) si=Doxygen::sectionDict[pd->name()];
      Doxygen::indexList.addContentsItem(FALSE,
                                         convertToHtml(pd->title(),TRUE),
                                         gd->getReference(),
                                         gd->getOutputFileBase(),
                                         si ? si->label.data() : 0);
    }

    // write subgroups
    if (hasSubGroups)
    {
      startIndexHierarchy(ol,level+1);
      if (Config_getBool("SORT_GROUP_NAMES"))
        gd->groupList->sort();
      QListIterator<GroupDef> gli(*gd->groupList);
      GroupDef *subgd = 0;
      for (gli.toFirst();(subgd=gli.current());++gli)
      {
        writeGroupTreeNode(ol,subgd,level+1,ftv);
      }
      endIndexHierarchy(ol,level+1); 
    }


    if (Config_getBool("TOC_EXPAND"))
    {
       writeGroupIndexItem(gd,gd->getMemberList(MemberList::docDefineMembers),
                         theTranslator->trDefines());
       writeGroupIndexItem(gd,gd->getMemberList(MemberList::docTypedefMembers),
                         theTranslator->trTypedefs());
       writeGroupIndexItem(gd,gd->getMemberList(MemberList::docEnumMembers),
                         theTranslator->trEnumerations());
       writeGroupIndexItem(gd,gd->getMemberList(MemberList::docFuncMembers),
                           fortranOpt ? theTranslator->trSubprograms() :
                           vhdlOpt    ? VhdlDocGen::trFunctionAndProc() :
                                        theTranslator->trFunctions()
                          );
       writeGroupIndexItem(gd,gd->getMemberList(MemberList::docVarMembers),
                         theTranslator->trVariables());
       writeGroupIndexItem(gd,gd->getMemberList(MemberList::docProtoMembers),
                         theTranslator->trFuncProtos());

      // write namespaces
      NamespaceSDict *namespaceSDict=gd->namespaceSDict;
      if (namespaceSDict->count()>0)
      {
        Doxygen::indexList.addContentsItem(TRUE,convertToHtml(fortranOpt?theTranslator->trModules():theTranslator->trNamespaces(),TRUE),gd->getReference(), gd->getOutputFileBase(), 0);
        Doxygen::indexList.incContentsDepth();

        NamespaceSDict::Iterator ni(*namespaceSDict);
        NamespaceDef *nsd;
        for (ni.toFirst();(nsd=ni.current());++ni)
        {
          Doxygen::indexList.addContentsItem(FALSE, convertToHtml(nsd->name(),TRUE), nsd->getReference(), nsd->getOutputFileBase(), 0);
        }
        Doxygen::indexList.decContentsDepth();
      }

      // write classes
      if (gd->classSDict->count()>0)
      {
        Doxygen::indexList.addContentsItem(TRUE,convertToHtml(fortranOpt?theTranslator->trDataTypes():theTranslator->trClasses(),TRUE), gd->getReference(), gd->getOutputFileBase(), 0);
        Doxygen::indexList.incContentsDepth();

        ClassDef *cd;
        ClassSDict::Iterator cdi(*gd->classSDict);
        for (cdi.toFirst();(cd=cdi.current());++cdi)
        {
          if (cd->isLinkable())
          {
            //printf("node: Has children %s\n",cd->name().data());
            Doxygen::indexList.addContentsItem(FALSE,cd->displayName(),cd->getReference(),cd->getOutputFileBase(),cd->anchor());
          }
        }

        //writeClassTree(gd->classSDict,1);
        Doxygen::indexList.decContentsDepth();
      }

      // write file list
      FileList *fileList=gd->fileList;
      if (fileList->count()>0)
      {
        Doxygen::indexList.addContentsItem(TRUE, 
              theTranslator->trFile(TRUE,FALSE),
              gd->getReference(), 
              gd->getOutputFileBase(), 0);
        Doxygen::indexList.incContentsDepth();

        FileDef *fd=fileList->first();
        while (fd)
        {
          Doxygen::indexList.addContentsItem(FALSE, convertToHtml(fd->name(),TRUE),fd->getReference(), fd->getOutputFileBase(), 0);
          fd=fileList->next();
        }
        Doxygen::indexList.decContentsDepth();
      }

      // write examples
      if (gd->exampleDict->count()>0)
      {
        Doxygen::indexList.addContentsItem(TRUE, convertToHtml(theTranslator->trExamples(),TRUE),gd->getReference(), gd->getOutputFileBase(), 0);
        Doxygen::indexList.incContentsDepth();

        PageSDict::Iterator eli(*(gd->exampleDict));
        PageDef *pd=eli.toFirst();
        while (pd)
        {
          Doxygen::indexList.addContentsItem(FALSE,pd->name(),pd->getReference(),pd->getOutputFileBase(),0); 
          pd=++eli;
        }

        Doxygen::indexList.decContentsDepth();
      }
    }
    ol.endIndexListItem();
    
    Doxygen::indexList.decContentsDepth();
    if (ftv)
      ftv->decContentsDepth();
    //gd->visited=TRUE;
  }
}

void writeGroupHierarchy(OutputList &ol, FTVHelp* ftv)
{
  if (ftv)
  {
    ol.pushGeneratorState(); 
    ol.disable(OutputGenerator::Html);
  }
  startIndexHierarchy(ol,0);
  if (Config_getBool("SORT_GROUP_NAMES"))
    Doxygen::groupSDict->sort();
  GroupSDict::Iterator gli(*Doxygen::groupSDict);
  GroupDef *gd;
  for (gli.toFirst();(gd=gli.current());++gli)
  {
    writeGroupTreeNode(ol,gd,0,ftv);
  }
  endIndexHierarchy(ol,0); 
  if (ftv)
  {
    ol.popGeneratorState(); 
  }
}

//----------------------------------------------------------------------------
void writeDirTreeNode(OutputList &ol, DirDef *dd, int level, FTVHelp* ftv)
{
  if (level>20)
  {
    warn(dd->getDefFileName(),dd->getDefLine(),
        "warning: maximum nesting level exceeded for directory %s: "
        "check for possible recursive directory relation!\n",dd->name().data()
        );
    return;
  }

  static bool tocExpand = Config_getBool("TOC_EXPAND");
  bool isDir = dd->subDirs().count()>0 || // there are subdirs
               (tocExpand &&              // or toc expand and
                dd->getFiles() && dd->getFiles()->count()>0 // there are files
               );
  //printf("gd=`%s': pageDict=%d\n",gd->name().data(),gd->pageDict->count());
  Doxygen::indexList.addContentsItem(isDir,dd->shortName(),dd->getReference(),dd->getOutputFileBase(),0); 
  Doxygen::indexList.incContentsDepth();
  if (ftv)
  {
    ftv->addContentsItem(isDir,dd->shortName(),dd->getReference(),dd->getOutputFileBase(),0); 
    ftv->incContentsDepth();
  }

  ol.startIndexListItem();
  ol.startIndexItem(dd->getReference(),dd->getOutputFileBase());
  ol.parseText(dd->shortName());
  ol.endIndexItem(dd->getReference(),dd->getOutputFileBase());
  if (dd->isReference()) 
  { 
    ol.startTypewriter(); 
    ol.docify(" [external]");
    ol.endTypewriter();
  }

  // write sub directories
  if (dd->subDirs().count()>0)
  {
    startIndexHierarchy(ol,level+1);
    QListIterator<DirDef> dli(dd->subDirs());
    DirDef *subdd = 0;
    for (dli.toFirst();(subdd=dli.current());++dli)
    {
      writeDirTreeNode(ol,subdd,level+1,ftv);
    }
    endIndexHierarchy(ol,level+1); 
  }

  if (tocExpand)
  {
    // write files of this directory
    FileList *fileList=dd->getFiles();
    if (fileList && fileList->count()>0)
    {
      FileDef *fd=fileList->first();
      while (fd)
      {
        Doxygen::indexList.addContentsItem(FALSE, convertToHtml(fd->name(),TRUE),fd->getReference(), fd->getOutputFileBase(), 0);
        fd=fileList->next();
      }
    }
  }
  ol.endIndexListItem();

  Doxygen::indexList.decContentsDepth();
  if (ftv)
    ftv->decContentsDepth();
}

void writeDirHierarchy(OutputList &ol, FTVHelp* ftv)
{
  if (ftv)
  {
    ol.pushGeneratorState(); 
    ol.disable(OutputGenerator::Html);
  }
  startIndexHierarchy(ol,0);
  SDict<DirDef>::Iterator dli(*Doxygen::directories);
  DirDef *dd;
  for (dli.toFirst();(dd=dli.current());++dli)
  {
    if (dd->getOuterScope()==Doxygen::globalScope) writeDirTreeNode(ol,dd,0,ftv);
  }
  endIndexHierarchy(ol,0); 
  if (ftv)
    ol.popGeneratorState(); 
}

//----------------------------------------------------------------------------

void writeGroupIndex(OutputList &ol)
{
  if (documentedGroups==0) return; 
  ol.pushGeneratorState(); 
  ol.disable(OutputGenerator::Man);
  LayoutNavEntry *lne = LayoutDocManager::instance().rootNavEntry()->find(LayoutNavEntry::Modules);
  QCString title = lne->title();
  startFile(ol,"modules",0,title,HLI_Modules);
  startTitle(ol,0);
  //QCString title = theTranslator->trModules();
  //if (!Config_getString("PROJECT_NAME").isEmpty()) 
  //{
  //  title.prepend(Config_getString("PROJECT_NAME")+" ");
  //}
  ol.parseText(title);
  endTitle(ol,0,0);
  ol.startContents();
  ol.startTextBlock();
  Doxygen::indexList.addContentsItem(TRUE,title,0,"modules",0); 
  Doxygen::indexList.incContentsDepth();
  ol.parseText(lne->intro());
  ol.endTextBlock();

  FTVHelp* ftv = 0;
  bool treeView=Config_getBool("USE_INLINE_TREES");
  if (treeView)
  {
    ftv = new FTVHelp(FALSE);
  }

  writeGroupHierarchy(ol,ftv);

  Doxygen::indexList.decContentsDepth();
  if (ftv)
  {
    QGString outStr;
    FTextStream t(&outStr);
    ftv->generateTreeViewInline(t);
    ol.pushGeneratorState(); 
    ol.disableAllBut(OutputGenerator::Html);
    ol.writeString(outStr);
    ol.popGeneratorState();
    delete ftv;
  }
  endFile(ol);
  ol.popGeneratorState();
}

//----------------------------------------------------------------------------

void writeDirIndex(OutputList &ol)
{
  if (documentedDirs==0) return; 
  ol.pushGeneratorState(); 
  ol.disable(OutputGenerator::Man);
  LayoutNavEntry *lne = LayoutDocManager::instance().rootNavEntry()->find(LayoutNavEntry::Dirs);
  QCString title = lne->title();
  startFile(ol,"dirs",0,title,HLI_Directories);
  startTitle(ol,0);
  //if (!Config_getString("PROJECT_NAME").isEmpty()) 
  //{
  //  title.prepend(Config_getString("PROJECT_NAME")+" ");
  //}
  ol.parseText(title);
  endTitle(ol,0,0);
  ol.startContents();
  ol.startTextBlock();
  Doxygen::indexList.addContentsItem(TRUE,title,0,"dirs",0); 
  Doxygen::indexList.incContentsDepth();
  ol.parseText(lne->intro());
  ol.endTextBlock();

  FTVHelp* ftv = 0;
  bool treeView=Config_getBool("USE_INLINE_TREES");
  if (treeView)
  {
    ftv = new FTVHelp(FALSE);
  }

  writeDirHierarchy(ol,ftv);

  if (ftv)
  {
    QGString outStr;
    FTextStream t(&outStr);
    ftv->generateTreeViewInline(t);
    ol.pushGeneratorState(); 
    ol.disableAllBut(OutputGenerator::Html);
    ol.writeString(outStr);
    ol.popGeneratorState();
    delete ftv;
  }
  Doxygen::indexList.decContentsDepth();
  endFile(ol);
  ol.popGeneratorState();
}

//----------------------------------------------------------------------------

static bool mainPageHasTitle()
{
  if (Doxygen::mainPage==0) return FALSE;
  if (Doxygen::mainPage->title().isEmpty()) return FALSE;
  if (Doxygen::mainPage->title().lower()=="notitle") return FALSE;
  return TRUE;
}

//----------------------------------------------------------------------------

void writeIndex(OutputList &ol)
{
  static bool fortranOpt = Config_getBool("OPTIMIZE_FOR_FORTRAN");
  static bool vhdlOpt    = Config_getBool("OPTIMIZE_OUTPUT_VHDL");
  // save old generator state
  ol.pushGeneratorState();

  QCString projPrefix;
  if (!Config_getString("PROJECT_NAME").isEmpty())
  {
    projPrefix=Config_getString("PROJECT_NAME")+" ";
  }

#if 0
  {
    QFile f(Config_getString("HTML_OUTPUT")+"/navindex.js");
    if (f.open(IO_WriteOnly))
    {
      FTextStream t(&f);
      t << "var NAVINDEX =" << endl;
      LayoutNavEntry *layout = LayoutDocManager::instance().rootNavEntry();
      bool first=TRUE;
      writeFullNavIndex(t,layout,0,first);
      t << endl << "];" << endl;
      t << endl << navindex_script;
    }
  }
#endif

  //--------------------------------------------------------------------
  // write HTML index
  //--------------------------------------------------------------------
  ol.disableAllBut(OutputGenerator::Html);

  QCString defFileName = 
    Doxygen::mainPage ? Doxygen::mainPage->getDefFileName().data() : "[generated]";
  int defLine =
    Doxygen::mainPage ? Doxygen::mainPage->getDefLine() : -1;

  QCString title;
  if (!mainPageHasTitle())
  {
    title = theTranslator->trMainPage();
  }
  else 
  {
    title = filterTitle(Doxygen::mainPage->title());
  }

  static bool generateTreeView = Config_getBool("GENERATE_TREEVIEW");
  //QCString indexName=Config_getBool("GENERATE_TREEVIEW")?"main":"index";
  QCString indexName="index";
  ol.startFile(indexName,0,title);
  
  if (Doxygen::mainPage)
  {
    Doxygen::indexList.addContentsItem(Doxygen::mainPage->hasSubPages(),title,0,indexName,0); 

    if (Doxygen::mainPage->hasSubPages())
    {
      writeSubPages(Doxygen::mainPage);
    }
  }

  ol.startQuickIndices();
  if (!Config_getBool("DISABLE_INDEX")) 
  {
    ol.writeQuickLinks(TRUE,HLI_Main);
  }
  ol.endQuickIndices();
  if (generateTreeView)
  {
    ol.writeSplitBar(indexName);
  }
  bool headerWritten=FALSE;
  if (Doxygen::mainPage && !Doxygen::mainPage->title().isEmpty())
  {
    if (Doxygen::mainPage->title().lower()!="notitle")
    {
      ol.startHeaderSection();
      ol.startTitleHead(0);
      ol.parseDoc(Doxygen::mainPage->docFile(),Doxygen::mainPage->docLine(),
                  Doxygen::mainPage,0,Doxygen::mainPage->title(),
                  TRUE,FALSE,0,TRUE,FALSE);
      headerWritten = TRUE;
    }
  }
  else
  {
    if (!Config_getString("PROJECT_NAME").isEmpty())
    {
      ol.startHeaderSection();
      ol.startTitleHead(0);
      ol.parseText(projPrefix+theTranslator->trDocumentation());
      headerWritten = TRUE;
    }
  }
  if (headerWritten)
  {
    ol.endTitleHead(0,0);
    ol.endHeaderSection();
  }
  ol.startContents();
#if 0
  // ol.newParagraph(); // FIXME:PARA
  if (!Config_getString("PROJECT_NUMBER").isEmpty())
  {
    ol.startProjectNumber();
    ol.parseDoc(defFileName,defLine,
                Doxygen::mainPage,0,
                Config_getString("PROJECT_NUMBER"),
                TRUE,FALSE,0,
                TRUE,FALSE);
    ol.endProjectNumber();
  }
#endif
  if (Config_getBool("DISABLE_INDEX") && Doxygen::mainPage==0) 
  {
    ol.writeQuickLinks(FALSE,HLI_Main);
  }

  if (Doxygen::mainPage)
  {
    Doxygen::insideMainPage=TRUE;
    ol.startTextBlock();
    ol.parseDoc(defFileName,defLine,Doxygen::mainPage,0,
                Doxygen::mainPage->documentation(),TRUE,FALSE
                /*,Doxygen::mainPage->sectionDict*/);
    ol.endTextBlock();

    if (!Config_getString("GENERATE_TAGFILE").isEmpty())
    {
       Doxygen::tagFile << "  <compound kind=\"page\">" << endl
                        << "    <name>"
                        << convertToXML(Doxygen::mainPage->name())
                        << "</name>" << endl
                        << "    <title>"
                        << convertToXML(Doxygen::mainPage->title())
                        << "</title>" << endl
                        << "    <filename>"
                        << convertToXML(Doxygen::mainPage->getOutputFileBase())
                        << "</filename>" << endl;

       Doxygen::mainPage->writeDocAnchorsToTagFile();
       Doxygen::tagFile << "  </compound>" << endl;
    }
    Doxygen::insideMainPage=FALSE;
  }
  
  endFile(ol);
  ol.disable(OutputGenerator::Html);
  
  //--------------------------------------------------------------------
  // write LaTeX/RTF index
  //--------------------------------------------------------------------
  ol.enable(OutputGenerator::Latex);
  ol.enable(OutputGenerator::RTF);

  ol.startFile("refman",0,0);
  ol.startIndexSection(isTitlePageStart);
  if (!Config_getString("LATEX_HEADER").isEmpty()) 
  {
    ol.disable(OutputGenerator::Latex);
  }

  if (projPrefix.isEmpty())
  {
    ol.parseText(theTranslator->trReferenceManual());
  }
  else
  {
    ol.parseText(projPrefix);
  }

  if (!Config_getString("PROJECT_NUMBER").isEmpty())
  {
    ol.startProjectNumber(); 
    ol.parseDoc(defFileName,defLine,Doxygen::mainPage,0,Config_getString("PROJECT_NUMBER"),FALSE,FALSE);
    ol.endProjectNumber();
  }
  ol.endIndexSection(isTitlePageStart);
  ol.startIndexSection(isTitlePageAuthor);
  ol.parseText(theTranslator->trGeneratedBy());
  ol.endIndexSection(isTitlePageAuthor);
  ol.enable(OutputGenerator::Latex);

  ol.lastIndexPage();
  if (Doxygen::mainPage)
  {
    ol.startIndexSection(isMainPage);
    if (mainPageHasTitle())
    {
      ol.parseText(Doxygen::mainPage->title());
    }
    else
    {
      ol.parseText(/*projPrefix+*/theTranslator->trMainPage());
    }
    ol.endIndexSection(isMainPage);
  }
  if (documentedPages>0)
  {
    //ol.parseText(projPrefix+theTranslator->trPageDocumentation());
    //ol.endIndexSection(isPageDocumentation);
    PageSDict::Iterator pdi(*Doxygen::pageSDict);
    PageDef *pd=pdi.toFirst();
    bool first=Doxygen::mainPage==0;
    for (pdi.toFirst();(pd=pdi.current());++pdi)
    {
      if (!pd->getGroupDef() && !pd->isReference() && 
          (!pd->hasParentPage() ||                    // not inside other page
           (Doxygen::mainPage==pd->getOuterScope()))  // or inside main page
         )
      {
        bool isCitationPage = pd->name()=="citelist";
        if (isCitationPage)
        {
          // For LaTeX the bibliograph is already written by \bibliography
          ol.pushGeneratorState();
          ol.disable(OutputGenerator::Latex);
        }
        QCString title = pd->title();
        if (title.isEmpty()) title=pd->name();
        ol.startIndexSection(isPageDocumentation);
        ol.parseText(title);
        ol.endIndexSection(isPageDocumentation);
        ol.pushGeneratorState(); // write TOC title (RTF only)
          ol.disableAllBut(OutputGenerator::RTF);
          ol.startIndexSection(isPageDocumentation2);
          ol.parseText(title);
          ol.endIndexSection(isPageDocumentation2);
          ol.popGeneratorState();
        ol.writeAnchor(0,pd->name());

        ol.writePageLink(pd->getOutputFileBase(),first);
        first=FALSE;

        if (isCitationPage)
        {
          ol.popGeneratorState();
        }
      }
    }
  }

  if (!Config_getBool("LATEX_HIDE_INDICES"))
  {
    //if (indexedPages>0)
    //{
    //  ol.startIndexSection(isPageIndex);
    //  ol.parseText(/*projPrefix+*/ theTranslator->trPageIndex());
    //  ol.endIndexSection(isPageIndex);
    //}
    if (documentedGroups>0)
    {
      ol.startIndexSection(isModuleIndex);
      ol.parseText(/*projPrefix+*/ theTranslator->trModuleIndex());
      ol.endIndexSection(isModuleIndex);
    }
    if (Config_getBool("SHOW_DIRECTORIES") && documentedDirs>0)
    {
      ol.startIndexSection(isDirIndex);
      ol.parseText(/*projPrefix+*/ theTranslator->trDirIndex());
      ol.endIndexSection(isDirIndex);
    }
    if (documentedNamespaces>0)
    {
      ol.startIndexSection(isNamespaceIndex);
      ol.parseText(/*projPrefix+*/(fortranOpt?theTranslator->trModulesIndex():theTranslator->trNamespaceIndex()));
      ol.endIndexSection(isNamespaceIndex);
    }
    if (hierarchyClasses>0)
    {
      ol.startIndexSection(isClassHierarchyIndex);
      ol.parseText(/*projPrefix+*/
          (fortranOpt ? theTranslator->trCompoundIndexFortran() : 
           vhdlOpt    ? VhdlDocGen::trDesignUnitIndex()         :
                        theTranslator->trCompoundIndex()
          ));
      ol.endIndexSection(isClassHierarchyIndex);
    }
    if (annotatedClassesPrinted>0)
    {
      ol.startIndexSection(isCompoundIndex);
      ol.parseText(/*projPrefix+*/
          (fortranOpt ? theTranslator->trCompoundIndexFortran() :
              vhdlOpt ? VhdlDocGen::trDesignUnitIndex()         : 
                        theTranslator->trCompoundIndex()
          ));
      ol.endIndexSection(isCompoundIndex);
    }
    if (documentedFiles>0)
    {
      ol.startIndexSection(isFileIndex);
      ol.parseText(/*projPrefix+*/theTranslator->trFileIndex());
      ol.endIndexSection(isFileIndex);
    }
  }
  if (documentedGroups>0)
  {
    ol.startIndexSection(isModuleDocumentation);
    ol.parseText(/*projPrefix+*/theTranslator->trModuleDocumentation());
    ol.endIndexSection(isModuleDocumentation);
  }
  if (Config_getBool("SHOW_DIRECTORIES") && documentedDirs>0)
  {
    ol.startIndexSection(isDirDocumentation);
    ol.parseText(/*projPrefix+*/theTranslator->trDirDocumentation());
    ol.endIndexSection(isDirDocumentation);
  }
  if (documentedNamespaces>0)
  {
    ol.startIndexSection(isNamespaceDocumentation);
    ol.parseText(/*projPrefix+*/(fortranOpt?theTranslator->trModuleDocumentation():theTranslator->trNamespaceDocumentation()));
    ol.endIndexSection(isNamespaceDocumentation);
  }
  if (annotatedClasses>0)
  {
    ol.startIndexSection(isClassDocumentation);
    ol.parseText(/*projPrefix+*/(fortranOpt?theTranslator->trTypeDocumentation():theTranslator->trClassDocumentation()));
    ol.endIndexSection(isClassDocumentation);
  }
  if (documentedFiles>0)
  {
    ol.startIndexSection(isFileDocumentation);
    ol.parseText(/*projPrefix+*/theTranslator->trFileDocumentation());
    ol.endIndexSection(isFileDocumentation);
  }
  if (Doxygen::exampleSDict->count()>0)
  {
    ol.startIndexSection(isExampleDocumentation);
    ol.parseText(/*projPrefix+*/theTranslator->trExampleDocumentation());
    ol.endIndexSection(isExampleDocumentation);
  }
  ol.endIndexSection(isEndIndex);
  endFile(ol);

  if (Doxygen::mainPage)
  {
    Doxygen::insideMainPage=TRUE;
    ol.disable(OutputGenerator::Man);
    startFile(ol,Doxygen::mainPage->name(),0,Doxygen::mainPage->title());
    ol.startContents();
    ol.startTextBlock();
    ol.parseDoc(defFileName,defLine,Doxygen::mainPage,0,
                Doxygen::mainPage->documentation(),FALSE,FALSE
               );
    ol.endTextBlock();
    endFile(ol);
    ol.enable(OutputGenerator::Man);
    Doxygen::insideMainPage=FALSE;
  }

  ol.popGeneratorState();
}



