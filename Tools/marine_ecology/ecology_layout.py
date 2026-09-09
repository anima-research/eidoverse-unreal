"""Deterministic temperate shelf layout. Coordinates and dimensions are cm."""
import math

def smooth(a,b,x):
 t=max(0.,min(1.,(x-a)/(b-a)))
 return t*t*(3-2*t)

def height(x,y):
 r=((x-5500)/15000)**2+(y/20000)**2
 z=-20100+17450*math.exp(-r*r)
 z+=1450*math.exp(-((x+2300)/5200)**2-((y-7000)/6200)**2)
 # A shallow sand pocket (~8 m) for light-demanding seagrass, separate from
 # the deeper station bench and kelp-covered outer reef.
 z+=1500*math.exp(-(x/2500)**2-((y-6000)/2600)**2)
 z+=85*math.sin(x/1300+y/2900)+55*math.sin(x/470-y/900)+25*math.sin(x/185+y/270)
 # Sandy drainage grooves and low bedding ridges, not a uniformly bumpy floor.
 z-=220*math.exp(-((x-5500-500*math.sin(y/1800))/1200)**2)
 # Exactly ground the existing four habitat landing pads; preserve moonpool gap.
 blend=1-smooth(850,1950,max(abs(x-8000),abs(y-1300)))
 return z*(1-blend)-3332*blend

def reserved(x,y,margin=0):
 # Habitat + service deck; submarine's starting footprint and rear exit lane.
 return (abs(x-8000)<1000+margin and abs(y-1400)<1050+margin) or (abs(x-6000)<750+margin and -5200-margin<y<-1800+margin)

REEFS=[(3600,-700,1100),(4600,1700,1000),(10200,900,1300),(9700,-3500,1000),(7000,4200,1400),(1800,4400,1500),(-1700,6400,1500),(11500,4700,1100)]
