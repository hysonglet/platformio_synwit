#ifndef MAP_Demo_H
#define MAP_Demo_H

/* Offline map preview on top of libs/map_view (MapViewV2) + libs/ltmb_parse:
 * reads every *.ltb in sd:/maps and shows it in the map view, rotating every
 * few seconds. There is no touch/button wired up in this example, so the
 * carousel is the only way to move between maps.
 */
void map_demo_init(void);     /* wire mv_port: files -> FatFs, log -> RTT */
void map_demo_create(void);   /* build the screen and open the first map */

#endif /* MAP_Demo_H */
