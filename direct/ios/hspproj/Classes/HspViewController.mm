/*
    File: HspViewController.m
*/

#import "HspViewController.h"


@implementation HspViewController

- (instancetype)init
{
    self = [super init];
    NSLog(@"Init HspViewController");
    return self;
}

- (void)dealloc
{
    [super dealloc];
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)setHspView:(HspView *)hspview
{
    [self setView:hspview];
    [hspview setParent:self];
}

- (void)controlBanner:(int)prm
{
    NSLog(@"controlBanner___");
}

- (void)viewDidLoad
{
    [super viewDidLoad];
}

// The original game is a 960x540 landscape window.
- (UIInterfaceOrientationMask)supportedInterfaceOrientations
{
    return UIInterfaceOrientationMaskLandscape;
}

- (BOOL)prefersStatusBarHidden
{
    return YES;
}

- (BOOL)prefersHomeIndicatorAutoHidden
{
    return YES;
}

- (void)viewDidAppear:(BOOL)animated
{
    [super viewDidAppear:animated];
    NSLog(@"viewDidAppear");
}

- (void)viewDidDisappear:(BOOL)animated
{
    [super viewDidDisappear:animated];
    NSLog(@"viewDidDisappear");
}


- (void)actMode:(int)amode
{
    HspView *hspview;
    hspview = (HspView *)self.view;
    [hspview actMode:amode];
    //NSLog(@"actmode%d",amode);
}

@end
