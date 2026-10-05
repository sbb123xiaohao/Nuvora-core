#ifndef NV_SETTINGS_UI_H
#define NV_SETTINGS_UI_H

/* Settings uses the same logical rectangles for painting and input. */
static void settings_card(struct nv_canvas *c,struct desktop_clip clip,
    u32 x,u32 y,u32 w,u32 h,u32 s) {
    desktop_round(c,clip,x,y,w*s,h*s,12*s,UI_WHITE);
}
static void settings_copy(struct nv_canvas *c,struct desktop_clip clip,
    u32 x,u32 y,u32 width,const char *value,u32 s,u32 color) {
    clip.right=MIN(clip.right,x+width*s);
    desktop_label(c,clip,x,y,value,(width+5)/6,s,color);
}
static void settings_button(struct nv_canvas *c,struct desktop_clip clip,
    u32 x,u32 y,struct desktop_rect r,const char *label,u32 s,
    bool enabled,bool selected,bool focus) {
    u32 fill=!enabled?0xf0f2f6:selected?UI_BLUE:0xf7f9fc;
    u32 border=enabled && focus?UI_BLUE:!enabled?0xf0f2f6:selected?UI_BLUE:0xe5eaf1;
    desktop_round(c,clip,x+r.x*s,y+r.y*s,r.w*s,r.h*s,7*s,border);
    if (r.w>2 && r.h>2)
        desktop_round(c,clip,x+(r.x+1)*s,y+(r.y+1)*s,(r.w-2)*s,(r.h-2)*s,6*s,fill);
    u32 length=strlen(label),text=desktop_text_width(label,length,s);
    u32 padding=r.w*s>text?(r.w*s-text)/2:7*s;
    struct desktop_clip inner=clip;
    inner.right=MIN(inner.right,x+(r.x+r.w-5)*s);
    desktop_text(c,inner,x+r.x*s+padding,y+(r.y+(r.h-17)/2)*s,
        label,length,s,!enabled?UI_MUTED:selected?UI_WHITE:UI_TEXT);
}
static void settings_rule(struct nv_canvas *c,struct desktop_clip clip,
    u32 x,u32 y,u32 w,u32 s) {
    desktop_box(c,clip,x,y,w*s,s,0xedf0f4);
}
static const char *settings_network_type(const struct nv_net_info *n) {
    return n->type==NV_NET_USB_BRIDGE?"USB network":n->type==NV_NET_WIFI?"Wi-Fi":
        n->type==NV_NET_WIRED?"Ethernet":"Unknown adapter";
}
static const char *settings_network_state(const struct desktop_view *v) {
    if (!v->network_present || !v->network) return "No network adapter";
    const struct nv_net_info *n=v->network;
    return n->state==NV_NET_ONLINE?"Connected":n->state==NV_NET_CONFIGURING?"Requesting an address":
        n->state==NV_NET_LINK?"Link ready":n->state==NV_NET_DOWN?"Link disconnected":"Driver unavailable";
}
static void settings_network_mac(char out[24],const struct nv_net_info *n) {
    static const char hex[]="0123456789ABCDEF";
    u32 at=0;
    for (u32 i=0;i<6;++i) {
        if (i) out[at++]=':';
        out[at++]=hex[n->mac[i]>>4];out[at++]=hex[n->mac[i]&15];
    }
    out[at]=0;
}
static void settings_detail(struct nv_canvas *c,struct desktop_clip clip,
    u32 x,u32 y,u32 width,const char *label,const char *value,u32 s) {
    settings_copy(c,clip,x,y,width,label,s,UI_MUTED);
    settings_copy(c,clip,x,y+19*s,width,value,s,UI_TEXT);
}
static void settings_sidebar(struct nv_canvas *c,struct desktop_clip clip,
    u32 x,u32 y,u32 w,u32 h,u32 s,const struct desktop_view *v,
    struct settings_layout g) {
    static const char *const tabs[]={"Appearance","Sound","Network","Security","System"};
    static const u32 icons[]={UI_ICON_IMAGE,UI_ICON_SPEAKER,UI_ICON_NETWORK,UI_ICON_SHIELD,UI_ICON_SETTINGS};
    desktop_box(c,clip,x,y,g.side*s,h*s,UI_WHITE);
    desktop_box(c,clip,x+(g.side-1)*s,y,s,h*s,0xe9edf3);
    ui_line_icon(c,clip,x+22*s,y+23*s,21*s,UI_ICON_SETTINGS,UI_BLUE);
    desktop_bold_text(c,clip,x+52*s,y+25*s,"Settings",8,s,UI_TEXT);
    desktop_text(c,clip,x+22*s,y+52*s,"Preferences",11,s,UI_MUTED);
    if (v->settings_confirm) {
        ui_line_icon(c,clip,x+26*s,y+96*s,18*s,UI_ICON_SHIELD,UI_BLUE);
        settings_copy(c,clip,x+52*s,y+96*s,g.side-64,"Power",s,UI_TEXT);
        settings_copy(c,clip,x+22*s,y+132*s,g.side-44,"Confirmation",s,UI_MUTED);
    } else for (u32 i=0;i<SETTINGS_TABS;++i) {
        struct desktop_rect r=desktop_settings_rect(w,h,0,i);
        bool selected=v->settings_tab==i;
        if (selected) desktop_round(c,clip,x+r.x*s,y+r.y*s,r.w*s,r.h*s,8*s,UI_BLUE);
        u32 text_x=g.side<176?30:42,icon_x=g.side<176?7:13;
        ui_line_icon(c,clip,x+(r.x+icon_x)*s,y+(r.y+10)*s,(g.side<176?16:18)*s,
            icons[i],selected?UI_WHITE:0x6f7b8d);
        settings_copy(c,clip,x+(r.x+text_x)*s,y+(r.y+10)*s,r.w-text_x-4,
            tabs[i],s,selected?UI_WHITE:UI_TEXT);
    }
    if (h>=390 && v->account) {
        settings_rule(c,clip,x+22*s,y+(h-72)*s,g.side-44,s);
        const char *name=v->account->info.display_name;
        settings_copy(c,clip,x+22*s,y+(h-57)*s,g.side-44,*name?name:"Local account",s,UI_TEXT);
        settings_copy(c,clip,x+22*s,y+(h-34)*s,g.side-44,
            v->account->info.role==NV_ACCOUNT_ADMIN?"Administrator":"Standard account",s,UI_MUTED);
    }
}
static void desktop_settings_render(struct nv_canvas *c,struct desktop_clip clip,
    u32 x,u32 y,u32 w,u32 h,u32 s,const struct desktop_view *v) {
    static const char *const tabs[]={"Appearance","Sound","Network","Security","System"};
    static const char *const subtitles[]={"Personalize your desktop and files.","Adjust your audio output.",
        "Manage your network connection.","Protect your account and session.","Device information and power."};
    static const u32 icons[]={UI_ICON_IMAGE,UI_ICON_SPEAKER,UI_ICON_NETWORK,UI_ICON_SHIELD,UI_ICON_SETTINGS};
    u32 tab=MIN(v->settings_tab,(u32)SETTINGS_TABS-1);
    struct settings_layout g=desktop_settings_layout(w,h,tab);
    if (v->settings_confirm) g=desktop_settings_layout(w,h,SETTINGS_SYSTEM);
    u32 dx=x+g.left*s,inner=g.width>32?g.width-32:1;
    desktop_box(c,clip,x,y,w*s,h*s,UI_BG);
    clip.left=MAX(clip.left,x);clip.top=MAX(clip.top,y);
    clip.right=MIN(clip.right,x+w*s);clip.bottom=MIN(clip.bottom,y+h*s);
    settings_sidebar(c,clip,x,y,w,h,s,v,g);
    settings_card(c,clip,dx,y+16*s,g.width,82,s);
    const char *heading=v->settings_confirm?"Power controls":tabs[tab];
    struct desktop_clip title_clip=clip;
    title_clip.right=MIN(title_clip.right,dx+(g.width-18)*s);
    desktop_bold_text(c,title_clip,dx+20*s,y+28*s,heading,strlen(heading),2*s,UI_TEXT);
    settings_copy(c,clip,dx+20*s,y+69*s,g.width-40,
        v->settings_confirm?"Review before continuing.":subtitles[tab],s,UI_MUTED);
    if (g.width>=400) {
        desktop_round(c,clip,dx+(g.width-54)*s,y+36*s,34*s,34*s,10*s,0xf2f6fe);
        ui_line_icon(c,clip,dx+(g.width-47)*s,y+43*s,20*s,
            v->settings_confirm?UI_ICON_SHIELD:icons[tab],UI_BLUE);
    }
    if (v->settings_confirm) {
        settings_card(c,clip,dx,y+114*s,g.width,h>146?h-146:1,s);
        const char *title=v->settings_confirm==NV_CTL_REBOOT?"Restart this computer?":"Shut down this computer?";
        desktop_bold_text(c,clip,dx+16*s,y+132*s,title,strlen(title),s,UI_TEXT);
        settings_copy(c,clip,dx+16*s,y+169*s,inner,"Unsaved changes will be checked first.",s,UI_MUTED);
        settings_copy(c,clip,dx+16*s,y+199*s,inner,"Mounted data volumes will be saved.",s,UI_MUTED);
        settings_copy(c,clip,dx+16*s,y+238*s,inner,"Choose Cancel to return to Settings.",s,UI_MUTED);
        for (u32 i=0;i<2;++i) {
            struct desktop_rect r=desktop_settings_rect(w,h,0,SETTINGS_CANCEL+i);
            settings_button(c,clip,x,y,r,i?"Continue":"Cancel",s,true,i,v->settings_focus==i);
        }
        return;
    }
    bool admin=v->account && v->account->info.role==NV_ACCOUNT_ADMIN;
    const char *buttons[6]={0};bool enabled[6]={true,true,true,true,true,true};
    if (tab==SETTINGS_APPEARANCE) {
        settings_card(c,clip,dx,y+114*s,g.width,84,s);
        desktop_bold_text(c,clip,dx+16*s,y+127*s,"Wallpaper",9,s,UI_TEXT);
        buttons[0]="Aurora";buttons[1]="Ocean";buttons[2]="Dusk";
        settings_card(c,clip,dx,y+218*s,g.width,154,s);
        desktop_bold_text(c,clip,dx+16*s,y+234*s,"File browser",12,s,UI_TEXT);
        buttons[3]="Space cards";buttons[4]="Compact list";
        settings_rule(c,clip,dx+16*s,y+312*s,inner,s);
        buttons[5]=v->show_hidden?"Hidden files: on":"Hidden files: off";
        if (h>=430) settings_copy(c,clip,dx+16*s,y+391*s,inner,"Saved with your account preferences.",s,UI_MUTED);
    } else if (tab==SETTINGS_SOUND) {
        settings_card(c,clip,dx,y+114*s,g.width,158,s);
        desktop_bold_text(c,clip,dx+16*s,y+130*s,"Output volume",13,s,UI_TEXT);
        char percent[16];u32 level=MIN(v->volume_percent,100u),at=number(percent,level,10);
        strlcpy(percent+at,"%",sizeof(percent)-at);
        u32 text=desktop_text_width(percent,strlen(percent),s);
        desktop_bold_text(c,clip,dx+(g.width-16)*s-text,y+130*s,percent,strlen(percent),s,UI_BLUE);
        struct desktop_rect slider=desktop_settings_slider_rect(w,h);
        u32 track_x=x+slider.x*s,track_y=y+(slider.y+11)*s;
        desktop_round(c,clip,track_x,track_y,slider.w*s,4*s,2*s,0xe4eaf3);
        if (level && v->audio_ready)
            desktop_round(c,clip,track_x,track_y,MAX(1u,(slider.w-1)*level/100)*s,4*s,2*s,UI_BLUE);
        u32 center=slider.x+(slider.w-1)*level/100;
        desktop_round(c,clip,x+(center-8)*s,y+(slider.y+5)*s,16*s,16*s,8*s,v->audio_ready?UI_BLUE:0xb8c2d2);
        desktop_round(c,clip,x+(center-4)*s,y+(slider.y+9)*s,8*s,8*s,4*s,UI_WHITE);
        settings_copy(c,clip,dx+16*s,y+197*s,inner,
            v->audio_ready?"Drag the slider to adjust the volume.":"No supported audio output is connected.",s,UI_MUTED);
        buttons[0]="Quieter";buttons[1]=v->volume_percent?"Mute":"Unmute";buttons[2]="Louder";
        enabled[0]=enabled[1]=enabled[2]=v->audio_ready;
        settings_card(c,clip,dx,y+288*s,g.width,88,s);
        ui_line_icon(c,clip,dx+16*s,y+306*s,20*s,UI_ICON_SPEAKER,UI_BLUE);
        desktop_bold_text(c,clip,dx+47*s,y+306*s,v->audio_ready?"Audio output":"Output unavailable",
            v->audio_ready?12:18,s,UI_TEXT);
        settings_copy(c,clip,dx+16*s,y+342*s,inner,
            v->audio_ready?"Changes apply immediately to playing media.":"Connect a supported HDA output to adjust sound.",s,UI_MUTED);
    } else if (tab==SETTINGS_NETWORK) {
        const struct nv_net_info *n=v->network;
        bool present=v->network_present && n;
        const char *status=settings_network_state(v);
        settings_card(c,clip,dx,y+114*s,g.width,158,s);
        desktop_bold_text(c,clip,dx+16*s,y+130*s,"Network connection",18,s,UI_TEXT);
        desktop_round(c,clip,dx+16*s,y+156*s,inner*s,50*s,8*s,present?UI_BLUE:0xf7f9fc);
        ui_line_icon(c,clip,dx+29*s,y+171*s,20*s,UI_ICON_NETWORK,present?UI_WHITE:UI_MUTED);
        char identity[48]="No adapter";
        if (present) {
            strlcpy(identity,settings_network_type(n),sizeof(identity));
            u32 at=strlen(identity);strlcpy(identity+at," / Adapter ",sizeof(identity)-at);at=strlen(identity);
            number(identity+at,n->index+1,10);
        }
        settings_copy(c,clip,dx+62*s,y+164*s,inner-62,identity,s,present?UI_WHITE:UI_TEXT);
        settings_copy(c,clip,dx+62*s,y+183*s,inner-62,status,s,present?0xdce9ff:UI_MUTED);
        buttons[0]="Next adapter";buttons[1]="Get address";buttons[2]="Refresh";
        enabled[0]=present;enabled[1]=admin && present && n->state>=NV_NET_LINK;
        u32 address_h=MIN(208u,h>304?h-304:1);
        settings_card(c,clip,dx,y+288*s,g.width,address_h,s);
        desktop_bold_text(c,clip,dx+16*s,y+303*s,g.details?"Router and DNS":"IPv4 configuration",
            g.details?14:18,s,UI_TEXT);
        if (present) {
            const char *labels[]={"Address","Subnet mask","Router","DNS"};
            u32 values[]={n->ip,n->mask,n->gateway,n->dns};
            if (g.details) {
                char value[24];desktop_ipv4(value,n->gateway);
                ui_line_icon(c,clip,dx+17*s,y+340*s,18*s,UI_ICON_NETWORK,UI_BLUE);
                settings_copy(c,clip,dx+45*s,y+331*s,inner-30,"Router",s,UI_MUTED);
                settings_copy(c,clip,dx+45*s,y+350*s,inner-30,value,s,UI_TEXT);
                if (address_h>=145) {
                    settings_rule(c,clip,dx+16*s,y+376*s,inner,s);
                    desktop_ipv4(value,n->dns);
                    ui_line_icon(c,clip,dx+17*s,y+400*s,18*s,UI_ICON_DOCUMENT,UI_BLUE);
                    settings_copy(c,clip,dx+45*s,y+390*s,inner-30,"DNS server",s,UI_MUTED);
                    settings_copy(c,clip,dx+45*s,y+409*s,inner-30,value,s,UI_TEXT);
                }
            } else {
                /* Compact windows keep all four actual address fields visible. */
                u32 column=(inner-12)/2,spacing=address_h>=128?50:address_h>=90?29:19;
                u32 first_row=address_h>=90?328:322;
                for (u32 i=0;i<4;++i) {
                    char value[24];desktop_ipv4(value,values[i]);
                    u32 px=dx+(16+(i%2)*(column+12))*s,py=y+(first_row+(i/2)*spacing)*s;
                    if (address_h>=128) settings_detail(c,clip,px,py,column,labels[i],value,s);
                    else {
                        char field[48];u32 at=strlcpy(field,i==0?"IP":i==1?"Mask":i==2?"GW":labels[i],sizeof(field));
                        strlcpy(field+at,": ",sizeof(field)-at);at=strlen(field);
                        strlcpy(field+at,value,sizeof(field)-at);
                        settings_copy(c,clip,px,py,column,field,s,UI_TEXT);
                    }
                }
            }
        } else {
            settings_copy(c,clip,dx+16*s,y+337*s,inner,"Connect a supported wired or USB adapter.",s,UI_MUTED);
            if (address_h>=120) settings_copy(c,clip,dx+16*s,y+368*s,inner,"Status updates automatically.",s,UI_MUTED);
        }
        if (g.details) {
            u32 rx=dx+(g.width+16)*s;
            settings_card(c,clip,rx,y+16*s,g.details,h-32,s);
            desktop_bold_text(c,clip,rx+20*s,y+34*s,"Network details",15,s,UI_TEXT);
            settings_rule(c,clip,rx+20*s,y+69*s,g.details-40,s);
            if (present) {
                settings_detail(c,clip,rx+20*s,y+86*s,g.details-40,"Adapter type",settings_network_type(n),s);
                const char *labels[]={"IP address","Subnet mask","Router","DNS server"};
                u32 values[]={n->ip,n->mask,n->gateway,n->dns};
                for (u32 i=0;i<4;++i) {
                    char value[24];desktop_ipv4(value,values[i]);
                    settings_detail(c,clip,rx+20*s,y+(136+i*50)*s,g.details-40,labels[i],value,s);
                }
                if (h>=450) {
                    char mac[24];settings_network_mac(mac,n);
                    settings_detail(c,clip,rx+20*s,y+336*s,g.details-40,"MAC address",mac,s);
                }
                if (h>=520) {
                    settings_rule(c,clip,rx+20*s,y+402*s,g.details-40,s);
                    settings_copy(c,clip,rx+20*s,y+421*s,g.details-40,status,s,UI_BLUE);
                }
            } else settings_copy(c,clip,rx+20*s,y+96*s,g.details-40,"No network adapter detected.",s,UI_MUTED);
        }
        if (h>=548) settings_copy(c,clip,dx+16*s,y+514*s,inner,
            admin?"Status updates automatically.":"Administrator access is required to get an address.",s,UI_MUTED);
    } else if (tab==SETTINGS_SECURITY) {
        settings_card(c,clip,dx,y+114*s,g.width,88,s);
        desktop_bold_text(c,clip,dx+16*s,y+127*s,"Lock automatically when idle",28,s,UI_TEXT);
        buttons[0]="1 minute";buttons[1]="5 minutes";buttons[2]="15 minutes";
        settings_card(c,clip,dx,y+218*s,g.width,148,s);
        desktop_bold_text(c,clip,dx+16*s,y+233*s,"Session protection",18,s,UI_TEXT);
        buttons[3]="Lock now";buttons[4]="Manage accounts";
        if (h>=412) settings_copy(c,clip,dx+16*s,y+386*s,inner,"A password is required to unlock your session.",s,UI_MUTED);
    } else {
        settings_card(c,clip,dx,y+114*s,g.width,126,s);
        desktop_bold_text(c,clip,dx+16*s,y+130*s,"Device information",18,s,UI_TEXT);
        settings_copy(c,clip,dx+16*s,y+156*s,inner,"Nuvora Core " NV_VERSION " / x86_64",s,UI_TEXT);
        settings_copy(c,clip,dx+16*s,y+178*s,inner,
            v->system_memory?v->system_memory:"Memory information unavailable",s,UI_MUTED);
        settings_copy(c,clip,dx+16*s,y+200*s,inner,v->system_free?v->system_free:"",s,UI_MUTED);
        char uptime[64];u32 at=number(uptime,v->uptime_minutes,10);
        strlcpy(uptime+at," min uptime / ",sizeof(uptime)-at);at=strlen(uptime);
        strlcpy(uptime+at,v->drive?v->drive:"RAM only",sizeof(uptime)-at);
        settings_copy(c,clip,dx+16*s,y+222*s,inner,uptime,s,UI_MUTED);
        settings_card(c,clip,dx,y+252*s,g.width,MIN(152u,h>260?h-260:1),s);
        buttons[0]="Save volumes";buttons[1]="Restart";buttons[2]="Shut down";
        enabled[1]=enabled[2]=admin;
        if (h>=450) settings_copy(c,clip,dx+16*s,y+425*s,inner,
            admin?"Power actions require confirmation.":"Ask an administrator to restart or shut down.",s,UI_MUTED);
    }
    for (u32 i=0;i<desktop_settings_choices(tab);++i) {
        struct desktop_rect r=desktop_settings_rect(w,h,tab,SETTINGS_ACTION+i);
        bool selected=tab==SETTINGS_APPEARANCE?(i<3?v->theme==i:i<5?v->file_view==i-3:v->show_hidden):
            tab==SETTINGS_SECURITY && i<3?v->idle_minutes==(i==0?1u:i==1?5u:15u):false;
        settings_button(c,clip,x,y,r,buttons[i],s,enabled[i],selected,v->settings_focus==i);
    }
}
#endif
