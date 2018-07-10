/*
    Xroach - A game of skill.  Try to find the roaches under your windows.

    Copyright 1991 by J.T. Anderson

    jta@locus.com

    This program may be freely distributed provided that all
    copyright notices are retained.

    To build:
      cc -I/usr/local/include/ -L/usr/local/lib/ -o xroach xroach.c -lm -lX11

    To run:
      ./xroach -speed 2 -squish -rc brown -rgc yellowgreen

    Dedicated to Greg McFarlane (gregm@otc.otca.oz.au).

    Squish option contributed by Rick Petkiewizc (rick@locus.com).

    Virtual root code adapted from patch sent by Colin Rafferty who
    borrowed it from Tom LaStrange. Several other folks sent similar
    fixes.

    Some glitches removed by patch from Guus Sliepen (guus@sliepen.warande.net)
    in 2001 (see https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=102668#5).

    Last update: 2018-JUL-10
*/

/* @(#)xroach.c	1.5 4/2/91 11:53:31 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
#include <X11/Xatom.h>

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <math.h>

char Copyright[] = "Xroach\nCopyright 1991 J.T. Anderson";

#include "roachmap.h"

typedef unsigned long Pixel;
typedef int ErrorHandler();

#define SCAMPER_EVENT (LASTEvent + 1)

#if !defined(GRAB_SERVER)
#define GRAB_SERVER    0
#endif

char         *display_name = NULL;
Display      *display;
GC           gc;
GC           gutsGC;
int          screen;
Pixel        black;
unsigned int display_height;
unsigned int display_width;
Window       rootWin;

Bool   squishRoach = False;
Bool   squishWinUp = False;
int    done        = 0;
int    errorVal    = 0;
int    eventBlock  = 0;
Pixmap squishMap;

typedef struct Roach
{
    RoachMap *rp;
    int      index;
    float    x;
    float    y;
    int      intX;
    int      intY;
    int      hidden;
    int      turnLeft;
    int      steps;
} Roach;

Roach *roaches;
int   maxRoaches = 10;
int   curRoaches = 0;
float roachSpeed = 20.0;
float turnSpeed  = 10.0;

Region rootVisible = NULL;

void Usage();
void SigHandler();
Window FindRootWindow();
int RandInt(int maxVal);
int RoachInRect(Roach *roach, int rx, int ry, int x, int y, unsigned int width, unsigned int height);
int RoachOverRect(Roach *roach, int rx, int ry, int x, int y, unsigned int width, unsigned int height);
void AddRoach();
void TurnRoach(Roach *roach);
void MoveRoach(int rx);
void DrawRoaches();
void CoverRoot();
int RoachErrors(XErrorEvent *err);
int CalcRootVisible();
int MarkHiddenRoaches();
Pixel AllocNamedColor(char *colorName, Pixel dfltPix);
void checkSquish(XButtonEvent *buttonEvent);

int main(int ac, char *av[])
{
    char                 *arg;
    char                 *gutsColor  = NULL;
    char                 *roachColor = "black";
    float                angle;
    int                  needCalc;
    int                  nVis;
    RoachMap             *rp;
    Window               squishWin;
    XEvent               ev;
    XGCValues            xgcv;
    XSetWindowAttributes xswa;

    /*
       Process command line options.
    */
    for (int ax = 1; ax < ac; ax++)
    {
        arg = av[ax];

        if (strcmp(arg, "-display") == 0)
            display_name = av[++ax];
        else if (strcmp(arg, "-rc") == 0)
            roachColor = av[++ax];
        else if (strcmp(arg, "-speed") == 0)
            roachSpeed = (float) strtod(av[++ax], (char **) NULL);
        else if (strcmp(arg, "-roaches") == 0)
            maxRoaches = (int) strtol(av[++ax], (char **) NULL, 0);
        else if (strcmp(arg, "-squish") == 0)
            squishRoach = True;
        else if (strcmp(arg, "-rgc") == 0)
            gutsColor = av[++ax];
        else
            Usage();
    }

    /* Compensate rate of turning for speed of movement. */
    turnSpeed = 200 / roachSpeed;

    if (turnSpeed < 1)
        turnSpeed = 1;

    srand((unsigned int) time((time_t *) NULL));

    /*
       Catch some signals so we can erase any visible roaches.
    */
    signal(SIGKILL, SigHandler);
    signal(SIGINT, SigHandler);
    signal(SIGTERM, SigHandler);
    signal(SIGHUP, SigHandler);

    display = XOpenDisplay(display_name);

    if (display == NULL)
    {
        if (display_name == NULL)
            display_name = getenv("DISPLAY");

        fprintf(stderr,
                "%s: cannot connect to X server %s\n",
                av[0],
                display_name ? display_name : "(default)");
        exit(1);
    }

    screen  = DefaultScreen(display);
    rootWin = FindRootWindow();
    black   = BlackPixel(display, screen);

    display_width  = (unsigned int) DisplayWidth(display, screen);
    display_height = (unsigned int) DisplayHeight(display, screen);

    /*
       Create roach pixmaps at several orientations.
    */
    for (int ax = 0; ax < 360; ax += ROACH_ANGLE)
    {
        int rx = ax / ROACH_ANGLE;
        angle = (float) (rx * 0.261799387799);
        rp = &roachPix[rx];
        rp->pixmap = XCreateBitmapFromData(display,
                                           rootWin,
                                           rp->roachBits,
                                           (unsigned int) rp->width,
                                           (unsigned int) rp->height);
        rp->sine = (float) sin(angle);
        rp->cosine = (float) cos(angle);
    }

    /*
      Create the squished pixmap
    */
    if (squishRoach)
        squishMap = XCreateBitmapFromData(display,
                                          rootWin,
                                          squish_bits,
                                          squish_width,
                                          squish_height);

    roaches = (Roach *) malloc(sizeof(Roach) * maxRoaches);

    gc = XCreateGC(display, rootWin, 0L, &xgcv);
    XSetForeground(display, gc, AllocNamedColor(roachColor, black));
    XSetFillStyle(display, gc, FillStippled);

    if (squishRoach && gutsColor != NULL)
    {
        gutsGC = XCreateGC(display, rootWin, 0L, &xgcv);
        XSetForeground(display, gutsGC, AllocNamedColor(gutsColor, black));
        XSetFillStyle(display, gutsGC, FillStippled);
    }
    else
    {
        gutsGC = gc;
    }

    while (curRoaches < maxRoaches)
        AddRoach();

    XSelectInput(display, rootWin, ExposureMask | SubstructureNotifyMask);

    if (squishRoach)
    {
        xswa.event_mask = ButtonPressMask;
        xswa.override_redirect = True;
        squishWin = XCreateWindow(display,
                                  rootWin,
                                  0, 0,
                                  display_width, display_height,
                                  0,
                                  CopyFromParent, InputOnly, CopyFromParent,
                                  CWOverrideRedirect | CWEventMask,
                                  &xswa);
        XLowerWindow(display, squishWin);
    }

    needCalc = 1;

    while (!done)
    {
        if (XPending(display))
        {
            XNextEvent(display, &ev);
        }
        else
        {
            if (needCalc)
                needCalc = CalcRootVisible();

            if (needCalc)
                nVis = 0;
            else
                nVis = MarkHiddenRoaches();

            ev.type = SCAMPER_EVENT;

            if (nVis)
            {
                if (!squishWinUp && squishRoach)
                {
                    XMapWindow(display, squishWin);
                    squishWinUp = True;
                }
            }
            else
            {
                if (squishWinUp && squishRoach)
                {
                    XUnmapWindow(display, squishWin);
                    squishWinUp = False;
                }

                /*
                if (needCalc == 0)
                    DrawRoaches();

                eventBlock = 1;
                XNextEvent(display, &ev);
                eventBlock = 0;
                */
            }
        }

        switch (ev.type)
        {
            case SCAMPER_EVENT:
                for (int rx = 0; rx < curRoaches; rx++)
                    if (!roaches[rx].hidden)
                        MoveRoach(rx);

                DrawRoaches();
                XFlush(display);
                usleep(20000);
                XSync(display, False);
                break;

            case UnmapNotify:
                if (ev.xunmap.window != squishWin)
                    needCalc = 1;
                break;

            case MapNotify:
            case Expose:
            case ConfigureNotify:
                needCalc = 1;
                break;

            case ButtonPress:
                checkSquish(&ev);
                done = !curRoaches;     /* Stop program if there are no more roaches */
                break;

            default:
                break;
        }
    }

    CoverRoot();
    XCloseDisplay(display);
    free(roaches);
    return 0;
}

#define USEPRT(msg) fprintf(stderr, msg)

void Usage()
{
    USEPRT("Usage: xroach [options]\n\n");
    USEPRT("Options:\n");
    USEPRT("       -display displayname\n");
    USEPRT("       -rc      roachcolor\n");
    USEPRT("       -roaches numroaches\n");
    USEPRT("       -speed   roachspeed\n");
    USEPRT("       -squish\n");
    USEPRT("       -rgc     roachgutscolor\n");

    exit(1);
}

void SigHandler()
{
    /*
       If we are blocked, no roaches are visible and we can just bail
       out.  If we are not blocked, then let the main procedure clean
       up the root window.
    */
    if (eventBlock)
    {
        XCloseDisplay(display);
        exit(0);
    }
    else
    {
        done = 1;
    }
}

/*
   Find the root or virtual root window.
*/
Window FindRootWindow()
{
    Atom          actualType;
    Atom          swmVroot;
    int           actualFormat;
    unsigned char *newRoot;
    unsigned int  numChildren;
    unsigned long bytesAfter;
    unsigned long nItems;
    Window        *children;
    Window        parentReturn;
    Window        realRoot;
    Window        rootReturn;
    Window        rootWin;

    /*
       Get real root window.
    */
    realRoot = rootWin = RootWindow(display, screen);

    /*
       Get atom for virtual root property.  If the atom doesn't
       exist, we can assume the corresponding property does not
       exist.
    */
    swmVroot = XInternAtom(display, "__SWM_VROOT", True);

    if (swmVroot == None)
        return rootWin;

    /*
       Run children of root, looking for a virtual root.
    */
    XQueryTree(display,
               rootWin,
               &rootReturn,
               &parentReturn,
               &children,
               &numChildren);

    for (int cx = 0; cx < numChildren; cx++)
    {
        newRoot = NULL;
        nItems = 0;

        if (XGetWindowProperty(display,
                               children[cx],
                               swmVroot,
                               0,
                               1,
                               False,
                               XA_WINDOW,
                               &actualType,
                               &actualFormat,
                               &nItems,
                               &bytesAfter,
                               &newRoot) == Success && actualFormat != None)
        {
            if (nItems >= 1)
                rootWin = *newRoot;

            if (newRoot)
                XFree(newRoot);
        }

        if (rootWin != realRoot)
            break;
    }

    XFree(children);

    return rootWin;
}

/*
   Generate random integer between 0 and maxVal-1.
*/
int RandInt(int maxVal)
{
    return rand() % maxVal;
}

/*
   Check for roach completely in specified rectangle.
*/
int RoachInRect(Roach *roach, int rx, int ry, int x, int y, unsigned int width, unsigned int height)
{
    if (rx < x)
        return 0;

    if ((rx + roach->rp->width) > (x + width))
        return 0;

    if (ry < y)
        return 0;

    if ((ry + roach->rp->height) > (y + height))
        return 0;

    return 1;
}

/*
   Check for roach overlapping specified rectangle.
*/
int RoachOverRect(Roach *roach, int rx, int ry, int x, int y, unsigned int width, unsigned int height)
{
    if (rx >= (x + width))
        return 0;

    if ((rx + roach->rp->width) <= x)
        return 0;

    if (ry >= (y + height))
        return 0;

    if ((ry + roach->rp->height) <= y)
        return 0;

    return 1;
}

/*
   Give birth to a roach.
*/
void AddRoach()
{
    Roach *r;

    if (curRoaches < maxRoaches)
    {
        r = &roaches[curRoaches++];
        r->index = RandInt(ROACH_HEADINGS);
        r->rp = &roachPix[r->index];
        r->x = RandInt(display_width - r->rp->width);
        r->y = RandInt(display_height - r->rp->height);
        r->intX = -1;
        r->intY = -1;
        r->hidden = 0;
        r->steps = RandInt((int) turnSpeed);
        r->turnLeft = RandInt(100) >= 50;
    }
}

/*
   Turn a roach.
*/
void TurnRoach(Roach *roach)
{
    if (roach->index != (roach->rp - roachPix))
        return;

    if (roach->turnLeft)
    {
        roach->index += (RandInt(30) / 10) + 1;

        if (roach->index >= ROACH_HEADINGS)
            roach->index -= ROACH_HEADINGS;
    }
    else
    {
        roach->index -= (RandInt(30) / 10) + 1;

        if (roach->index < 0)
            roach->index += ROACH_HEADINGS;
    }
}

/*
   Move a roach.
*/
void MoveRoach(int rx)
{
    float newX;
    float newY;
    Roach *roach;

    roach = &roaches[rx];
    newX = roach->x + (roachSpeed * roach->rp->cosine);
    newY = roach->y - (roachSpeed * roach->rp->sine);

    if (RoachInRect(roach,
                    (int) newX, (int) newY,
                    0, 0,
                    display_width, display_height))
    {
        roach->x = newX;
        roach->y = newY;

        if (roach->steps-- <= 0)
        {
            TurnRoach(roach);
            roach->steps = RandInt((int) turnSpeed);

            /*
               Previously, roaches would just go around in circles.
               This makes their movement more interesting (and disgusting too!).
            */
            if (RandInt(100) >= 80)
                roach->turnLeft ^= 1;
        }

        /* This is a kind of anti-collision algorithm which doesn't do what it is supposed to do,
           it eats CPU time and sometimes makes roaches spin around very crazy. Therefore it is
           commented out.

        for (int ii = rx + 2; ii < curRoaches; ii++) {
            r2 = &roaches[ii];

            if (RoachOverRect(roach,
                              (int) newX, (int) newY,
                              r2->intX, r2->intY,
                              (unsigned int) r2->rp->width, (unsigned int) r2->rp->height))
                TurnRoach(roach);

        } */
    }
    else
    {
        TurnRoach(roach);
    }
}

/*
   Draw all roaches.
*/
void DrawRoaches()
{
    Roach *roach;

    for (int rx = 0; rx < curRoaches; rx++)
    {
        roach = &roaches[rx];

        if (!roach->hidden)
        {
            XClearArea(display,
                       rootWin,
                       roach->intX,
                       roach->intY,
                       (unsigned int) roach->rp->width,
                       (unsigned int) roach->rp->height,
                       False);

            roach->intX = (int) roach->x;
            roach->intY = (int) roach->y;
            roach->rp = &roachPix[roach->index];

            XSetStipple(display, gc, roach->rp->pixmap);
            XSetTSOrigin(display, gc, roach->intX, roach->intY);
            XFillRectangle(display,
                           rootWin,
                           gc,
                           roach->intX,
                           roach->intY,
                           (unsigned int) roach->rp->width,
                           (unsigned int) roach->rp->height);
        }
        else
        {
            roach->intX = -1;
        }
    }
}

/*
   Cover root window to erase roaches.
*/
void CoverRoot()
{
    long                 wamask;
    Window               roachWin;
    XSetWindowAttributes xswa;

    xswa.background_pixmap = ParentRelative;
    xswa.override_redirect = True;
    wamask = CWBackPixmap | CWOverrideRedirect;
    roachWin = XCreateWindow(display, rootWin,
                             0, 0,
                             display_width, display_height,
                             0,
                             CopyFromParent, InputOutput, CopyFromParent,
                             (unsigned long) wamask, &xswa);
    XLowerWindow(display, roachWin);
    XMapWindow(display, roachWin);
    XFlush(display);
}

#if !GRAB_SERVER
int RoachErrors(XErrorEvent *err)
{
    errorVal = err->error_code;

    return 0;
}
#endif /* GRAB_SERVER */

/*
   Calculate visible region of root window.
*/
int CalcRootVisible()
{
    int               winX, winY;
    Region            covered;
    Region            visible;
    unsigned int      borderWidth;
    unsigned int      depth;
    unsigned int      nChildren;
    unsigned int      winHeight, winWidth;
    Window            *children;
    Window            dummy;
    XRectangle        rect;
    XWindowAttributes wa;

    /*
       If we don't grab the server, the XGetWindowAttribute or XGetGeometry
       calls can abort us.  On the other hand, the server grabs can make for
       some annoying delays.
    */
#if GRAB_SERVER
    XGrabServer(display);
#else
    XSetErrorHandler(RoachErrors);
#endif

    /*
       Get children of root.
    */
    XQueryTree(display, rootWin, &dummy, &dummy, &children, &nChildren);

    /*
       For each mapped child, add the window rectangle to the covered
       region.
    */
    covered = XCreateRegion();

    for (int wx = 0; wx < nChildren; wx++)
    {
        if (XEventsQueued(display, QueuedAlready))
        {
            XDestroyRegion(covered);
            return 1;
        }

        errorVal = 0;
        XGetWindowAttributes(display, children[wx], &wa);

        if (errorVal)
            continue;

        if (wa.class == InputOutput && wa.map_state == IsViewable)
        {
            XGetGeometry(display, children[wx], &dummy,
                         &winX, &winY,
                         &winWidth, &winHeight,
                         &borderWidth, &depth);

            if (errorVal)
                continue;

            rect.x = (short) winX;
            rect.y = (short) winY;

            rect.width = (unsigned short) (winWidth + (borderWidth * 2));
            rect.height = (unsigned short) (winHeight + (borderWidth * 2));

            XUnionRectWithRegion(&rect, covered, covered);
        }
    }

    XFree(children);

#if GRAB_SERVER
    XUngrabServer(display);
#else
    XSetErrorHandler((ErrorHandler *) NULL);
#endif

    /*
       Subtract the covered region from the root window region.
    */
    visible = XCreateRegion();

    rect.x = 0;
    rect.y = 0;

    rect.width  = (unsigned short) display_width;
    rect.height = (unsigned short) display_height;

    XUnionRectWithRegion(&rect, visible, visible);
    XSubtractRegion(visible, covered, visible);
    XDestroyRegion(covered);

    /*
       Save visible region globally.
    */
    if (rootVisible)
        XDestroyRegion(rootVisible);

    rootVisible = visible;

    /*
       Mark all roaches visible.
    */
    for (int wx = 0; wx < curRoaches; wx++)
        roaches[wx].hidden = 0;

    return 0;
}

/*
   Mark hidden roaches.
*/
int MarkHiddenRoaches()
{
    int   nVisible;
    Roach *r;

    nVisible = 0;

    for (int rx = 0; rx < curRoaches; rx++)
    {
        r = &roaches[rx];

        if (!r->hidden)
        {
            if (r->intX > 0 && XRectInRegion(rootVisible,
                                             r->intX,
                                             r->intY,
                                             (unsigned int) r->rp->width,
                                             (unsigned int) r->rp->height) == RectangleOut)
                r->hidden = 1;
            else
                nVisible++;
        }
    }

    return nVisible;
}

/*
   Allocate a color by name.
*/
Pixel AllocNamedColor(char *colorName, Pixel dfltPix)
{
    Pixel  pix;
    XColor exactcolor;
    XColor scrncolor;

    if (XAllocNamedColor(display,
                         DefaultColormap(display, screen),
                         colorName,
                         &scrncolor,
                         &exactcolor))
        pix = scrncolor.pixel;
    else
        pix = dfltPix;

    return pix;
}

/*
 * Check to see if we have to squish any roaches.
 */
void checkSquish(XButtonEvent *buttonEvent)
{
    int   x;
    int   y;
    Roach *r;

    x = buttonEvent->x;
    y = buttonEvent->y;

    for (int rx = 0; rx < curRoaches; rx++)
    {
        r = &roaches[rx];

        if (r->rp == NULL)
            continue;

        if (x > r->intX && x < (r->intX + r->rp->width) && y > r->intY && y < (r->intY + r->rp->height))
        {
            XSetStipple(display, gutsGC, squishMap);
            XSetTSOrigin(display, gutsGC, r->intX, r->intY);
            XFillRectangle(display,
                           rootWin,
                           gutsGC,
                           r->intX,
                           r->intY,
                           squish_width,
                           squish_height);
            /*
            * Delete the roach
            */
            for (int i = rx; i < curRoaches - 1; i++)
                roaches[i] = roaches[i + 1];

            curRoaches--;
            rx--;
        }
    }
}
