#define _POSIX_C_SOURCE 200809L
#include "client.h"
#include "content_fingerprint.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define PACKET_CAP 32768
#define QUEUE_CAP 64
typedef struct {uint32_t seq;Input input;uint16_t yaw;} Sample;
typedef struct {uint8_t bytes[96];unsigned len;} Outgoing;
struct MpClient {
    pthread_t thread;pthread_mutex_t mutex;
    int stop,connected,fd,credentials_ready;
    char host[256],identity_path[512];unsigned port;
    MpAccountId account;MpPlayerId player;MpDeviceId device;MpSessionId session;MpWorldId world;
    uint8_t credential[32],content_hash[32],scope[16],targets[6][16];
    unsigned roles[6],seed,revision;
    uint64_t action_sequence;
    Sample pending[QUEUE_CAP];unsigned pending_n,remainder;uint32_t input_sequence;
    Outgoing out[QUEUE_CAP];unsigned out_head,out_count;
    uint8_t state[PACKET_CAP];size_t state_n;uint32_t ack;int state_ready,welcome_ready;
    int repair_requested;
    Pos authority_start,authority_last;int pose_ready;
    MpPeer peers[MP_PEER_CAP],previous[MP_PEER_CAP];unsigned peer_n,previous_n;
    uint64_t snapshot_us,previous_us;
    uint64_t sent,received,snapshots,reconciliations,reconnects,receipts,successful_actions;
    unsigned peers_seen,queue_overflow;
    Outgoing journal[QUEUE_CAP];unsigned journal_n;
    char journal_path[544];
};
static uint64_t now(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return(uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static uint16_t u16(const uint8_t *p) {return(uint16_t)(p[0]|p[1]<<8);}
static uint32_t u32(const uint8_t *p) {return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t u64(const uint8_t *p) {return u32(p)|((uint64_t)u32(p+4)<<32);}
static void put16(uint8_t *p,unsigned n) {p[0]=n;p[1]=n>>8;}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++)p[i]=n>>(8*i);}
static void put64(uint8_t *p,uint64_t n) {for(unsigned i=0;i<8;i++)p[i]=n>>(8*i);}
static int stopping(MpClient *c) {pthread_mutex_lock(&c->mutex);int n=c->stop;pthread_mutex_unlock(&c->mutex);return n;}
static int send_bytes(int fd,const uint8_t *p,size_t n) {
    while(n){ssize_t k=send(fd,p,n,MSG_NOSIGNAL);if(k<=0)return 0;p+=k;n-=(size_t)k;}return 1;
}
static int recv_bytes(MpClient *c,int fd,uint8_t *p,size_t n) {
    uint64_t deadline=now()+3000000;
    while(n&&!stopping(c)) {
        struct pollfd poller={fd,POLLIN,0};int ready=poll(&poller,1,50);
        if(ready<0)return 0;if(!ready){if(now()>deadline)return 0;continue;}
        ssize_t k=recv(fd,p,n,0);if(k<=0)return 0;p+=k;n-=(size_t)k;
    }return !n;
}
static int send_packet(MpClient *c,int kind,const uint8_t *p,unsigned n) {
    uint8_t head[12];memcpy(head,"ANP1",4);put16(head+4,1);put16(head+6,kind);put32(head+8,n);
    int ok=send_bytes(c->fd,head,sizeof(head))&&send_bytes(c->fd,p,n);
    if(ok){pthread_mutex_lock(&c->mutex);c->sent+=n+12;pthread_mutex_unlock(&c->mutex);}return ok;
}
static int read_packet(MpClient *c,int *kind,uint8_t *payload,unsigned *n) {
    uint8_t head[12];if(!recv_bytes(c,c->fd,head,12))return 0;
    if(memcmp(head,"ANP1",4)||u16(head+4)!=1||u32(head+8)>PACKET_CAP)return 0;
    *kind=u16(head+6);*n=u32(head+8);if(!recv_bytes(c,c->fd,payload,*n))return 0;
    pthread_mutex_lock(&c->mutex);c->received+=*n+12;pthread_mutex_unlock(&c->mutex);return 1;
}
static int sync_parent(const char *path) {
    char parent[544];snprintf(parent,sizeof(parent),"%s",path);char *slash=strrchr(parent,'/');
    if(!slash)snprintf(parent,sizeof(parent),".");else if(slash==parent)slash[1]=0;else *slash=0;
    int fd=open(parent,O_RDONLY);if(fd<0)return 0;
    int ok=fsync(fd)==0;if(close(fd))ok=0;return ok;
}
static int credentials_save(MpClient *c) {
    /* Development credentials only. No MAC identity, never log the secret.
     * Owner-only POSIX file; native credential storage is a later adapter. */
    uint8_t data[272];memcpy(data,"ANI3",4);memcpy(data+4,c->account.bytes,16);
    memcpy(data+20,c->player.bytes,16);memcpy(data+36,c->device.bytes,16);
    memcpy(data+52,c->world.bytes,16);memcpy(data+68,c->credential,32);
    put32(data+100,c->seed);memcpy(data+104,c->content_hash,32);memcpy(data+136,c->scope,16);
    for(unsigned i=0;i<6;i++){put32(data+152+i*20,c->roles[i]);memcpy(data+156+i*20,c->targets[i],16);}
    int fd=open(c->identity_path,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return 0;
    ssize_t wrote=write(fd,data,sizeof(data));int ok=wrote==(ssize_t)sizeof(data)&&fsync(fd)==0;
    if(close(fd)!=0)ok=0;if(ok)ok=sync_parent(c->identity_path);
    if(!ok)unlink(c->identity_path);return ok;
}
static int journal_save(MpClient *c) {
    char temp[568];snprintf(temp,sizeof(temp),"%s.tmp.XXXXXX",c->journal_path);
    int fd=mkstemp(temp);if(fd<0)return 0;
    uint8_t head[8];memcpy(head,"ANJ1",4);put32(head+4,c->journal_n);
    int ok=write(fd,head,8)==8;
    for(unsigned i=0;ok&&i<c->journal_n;i++)ok=write(fd,c->journal[i].bytes,57)==57;
    if(fsync(fd))ok=0;if(close(fd))ok=0;
    if(ok)ok=rename(temp,c->journal_path)==0;
    if(ok)ok=sync_parent(c->journal_path);
    if(!ok)unlink(temp);return ok;
}
static int connect_socket(MpClient *c) {
    struct addrinfo hint={0},*list=NULL;hint.ai_socktype=SOCK_STREAM;hint.ai_family=AF_UNSPEC;
    char port[16];snprintf(port,sizeof(port),"%u",c->port);
    if(getaddrinfo(c->host,port,&hint,&list))return -1;
    int fd=-1;
    for(struct addrinfo *a=list;a&&!stopping(c);a=a->ai_next) {
        fd=socket(a->ai_family,a->ai_socktype,a->ai_protocol);if(fd<0)continue;
        fcntl(fd,F_SETFL,O_NONBLOCK);
        int result=connect(fd,a->ai_addr,a->ai_addrlen);
        if(result&&errno==EINPROGRESS) {
            struct pollfd p={fd,POLLOUT,0};int error=0;socklen_t size=sizeof(error);
            result=poll(&p,1,1000)>0&&getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)==0&&!error?0:-1;
        }
        if(!result){fcntl(fd,F_SETFL,0);struct timeval tv={2,0};setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));break;}
        close(fd);fd=-1;
    }freeaddrinfo(list);return fd;
}
static void snapshot(MpClient *c,const uint8_t *p,unsigned n) {
    if(n<72)return;
    unsigned bytes=u16(p+32),count=u16(p+34);
    if(count>MP_PEER_CAP||n!=72+bytes+count*64||bytes>PACKET_CAP)return;
    if(crc32(p+72,bytes)!=u32(p+36)) {
        pthread_mutex_lock(&c->mutex);c->repair_requested=1;pthread_mutex_unlock(&c->mutex);return;
    }
    pthread_mutex_lock(&c->mutex);
    c->ack=u32(p);c->revision=u32(p+4);memcpy(c->scope,p+16,16);
    memcpy(c->state,p+72,bytes);c->state_n=bytes;c->state_ready=1;
    memcpy(c->previous,c->peers,sizeof(c->peers));c->previous_n=c->peer_n;c->previous_us=c->snapshot_us;
    c->peer_n=count;c->snapshot_us=now();c->snapshots++;
    if(count>c->peers_seen)c->peers_seen=count;
    for(unsigned i=0;i<count;i++) {
        const uint8_t *peer=p+72+bytes+i*64;MpPeer *out=&c->peers[i];
        memcpy(out->id.bytes,peer,16);out->pos=(Pos){(int32_t)u32(peer+32),(int32_t)u32(peer+36),(int32_t)u32(peer+40)};
        out->facing=(int)u32(peer+56);out->phase=u32(peer+60);
    }
    pthread_mutex_unlock(&c->mutex);
}
static void *worker(void *context) {
    MpClient *c=context;uint8_t *packet=malloc(PACKET_CAP);if(!packet)return NULL;
    while(!stopping(c)) {
        c->fd=connect_socket(c);
        if(c->fd<0){struct timespec wait={0,200000000};nanosleep(&wait,NULL);continue;}
        uint8_t auth[52];put32(auth,1);memcpy(auth+4,c->device.bytes,16);memcpy(auth+20,c->credential,32);
        int kind;unsigned n;
        if(!send_packet(c,1,auth,sizeof(auth))||!read_packet(c,&kind,packet,&n)||kind!=2||n!=296)goto lost;
        if(memcmp(packet+80,mp_content_hash,32)||u32(packet+148)!=1)goto lost;
        pthread_mutex_lock(&c->mutex);
        if(c->credentials_ready&&(memcmp(c->account.bytes,packet,16)||memcmp(c->player.bytes,packet+16,16)||
            memcmp(c->device.bytes,packet+32,16)||memcmp(c->world.bytes,packet+64,16))) {
            pthread_mutex_unlock(&c->mutex);goto lost;
        }
        memcpy(c->account.bytes,packet,16);memcpy(c->player.bytes,packet+16,16);memcpy(c->device.bytes,packet+32,16);
        memcpy(c->session.bytes,packet+48,16);memcpy(c->world.bytes,packet+64,16);memcpy(c->content_hash,packet+80,32);
        if(memcmp(packet+112,(uint8_t[32]){0},32)) {
            memcpy(c->credential,packet+112,32);
        }
        c->seed=u32(packet+144);if(u64(packet+152)>c->action_sequence)c->action_sequence=u64(packet+152);
        memcpy(c->scope,packet+160,16);
        for(unsigned i=0;i<6;i++){c->roles[i]=u32(packet+176+i*20);memcpy(c->targets[i],packet+180+i*20,16);}
        if(!c->credentials_ready) {
            if(!credentials_save(c)){pthread_mutex_unlock(&c->mutex);goto lost;}
            c->credentials_ready=1;
        }
        c->input_sequence=c->pending_n=c->out_count=c->out_head=c->remainder=0;
        for(unsigned i=0;i<c->journal_n;i++){c->out[i]=c->journal[i];c->out_count++;}
        c->connected=c->welcome_ready=1;c->reconnects++;
        pthread_mutex_unlock(&c->mutex);
        uint64_t last_retry=now();
        while(!stopping(c)) {
            pthread_mutex_lock(&c->mutex);int repair=c->repair_requested;c->repair_requested=0;pthread_mutex_unlock(&c->mutex);
            if(repair&&!send_packet(c,4,NULL,0))goto lost;
            if(now()-last_retry>1000000) {
                pthread_mutex_lock(&c->mutex);
                for(unsigned i=0;i<c->journal_n&&c->out_count<QUEUE_CAP;i++) {
                    unsigned at=(c->out_head+c->out_count++)%QUEUE_CAP;c->out[at]=c->journal[i];
                }
                pthread_mutex_unlock(&c->mutex);last_retry=now();
                if(!send_packet(c,4,NULL,0))goto lost;
            }
            for(unsigned i=0;i<QUEUE_CAP;i++) {
                Outgoing out;
                pthread_mutex_lock(&c->mutex);int have=c->out_count>0;
                if(have){out=c->out[c->out_head];c->out_head=(c->out_head+1)%QUEUE_CAP;c->out_count--;}
                pthread_mutex_unlock(&c->mutex);
                if(!have)break;
                if(!send_packet(c,out.bytes[0],out.bytes+1,out.len-1))goto lost;
            }
            struct pollfd p={c->fd,POLLIN,0};int result=poll(&p,1,5);if(result<0)goto lost;
            if(!result)continue;
            if(!read_packet(c,&kind,packet,&n))goto lost;
            if(kind==4)snapshot(c,packet,n);
            else if(kind==6&&n==16){
                pthread_mutex_lock(&c->mutex);c->receipts++;if(!u32(packet+8))c->successful_actions++;
                for(unsigned i=0;i<c->journal_n;i++)if(u64(c->journal[i].bytes+1)==u64(packet)) {
                    memmove(c->journal+i,c->journal+i+1,(c->journal_n-i-1)*sizeof(Outgoing));c->journal_n--;
                    journal_save(c);break;
                }
                pthread_mutex_unlock(&c->mutex);
            }
            else goto lost;
        }
lost:
        close(c->fd);c->fd=-1;pthread_mutex_lock(&c->mutex);c->connected=0;c->state_ready=0;c->out_count=c->pending_n=0;pthread_mutex_unlock(&c->mutex);
        struct timespec wait={0,200000000};nanosleep(&wait,NULL);
    }free(packet);return NULL;
}
MpClient *mp_open(const char *host,unsigned port,const char *path) {
    if(!host||strlen(host)>=256||!path||strlen(path)>=512||!port||port>65535)return NULL;
    MpClient *c=calloc(1,sizeof(*c));if(!c)return NULL;
    snprintf(c->host,sizeof(c->host),"%s",host);snprintf(c->identity_path,sizeof(c->identity_path),"%s",path);c->port=port;c->fd=-1;
    snprintf(c->journal_path,sizeof(c->journal_path),"%s.history",path);
    FILE *f=fopen(path,"rb");
    if(f){uint8_t data[272];struct stat st;int secure=fstat(fileno(f),&st)==0&&!(st.st_mode&077);
        size_t n=fread(data,1,sizeof(data),f);int extra=fgetc(f);fclose(f);
        if(!secure||n!=272||extra!=EOF||memcmp(data,"ANI3",4)||memcmp(data+104,mp_content_hash,32)){free(c);return NULL;}
        memcpy(c->account.bytes,data+4,16);memcpy(c->player.bytes,data+20,16);memcpy(c->device.bytes,data+36,16);
        memcpy(c->world.bytes,data+52,16);memcpy(c->credential,data+68,32);c->credentials_ready=1;
        c->seed=u32(data+100);memcpy(c->content_hash,data+104,32);memcpy(c->scope,data+136,16);
        for(unsigned i=0;i<6;i++){c->roles[i]=u32(data+152+i*20);memcpy(c->targets[i],data+156+i*20,16);}
    }else if(errno!=ENOENT){free(c);return NULL;}
    f=fopen(c->journal_path,"rb");
    if(f){
        uint8_t head[8];if(fread(head,1,8,f)!=8||memcmp(head,"ANJ1",4)||u32(head+4)>QUEUE_CAP){fclose(f);free(c);return NULL;}
        c->journal_n=u32(head+4);
        for(unsigned i=0;i<c->journal_n;i++) {
            if(fread(c->journal[i].bytes,1,57,f)!=57){fclose(f);free(c);return NULL;}
            c->journal[i].len=57;uint64_t seq=u64(c->journal[i].bytes+1);
            if(seq>c->action_sequence)c->action_sequence=seq;
        }
        int extra=fgetc(f);fclose(f);if(extra!=EOF){free(c);return NULL;}
    }else if(errno!=ENOENT){free(c);return NULL;}
    pthread_mutex_init(&c->mutex,NULL);
    if(pthread_create(&c->thread,NULL,worker,c)){pthread_mutex_destroy(&c->mutex);free(c);return NULL;}return c;
}
void mp_close(MpClient *c) {if(!c)return;pthread_mutex_lock(&c->mutex);c->stop=1;pthread_mutex_unlock(&c->mutex);
    pthread_join(c->thread,NULL);pthread_mutex_destroy(&c->mutex);memset(c->credential,0,sizeof(c->credential));free(c);}
int mp_connected(MpClient *c) {if(!c)return 0;pthread_mutex_lock(&c->mutex);int n=c->connected;pthread_mutex_unlock(&c->mutex);return n;}
int mp_local_cache(MpClient *c,Game *g,int load) {
    if(!c)return 0;char path[544];snprintf(path,sizeof(path),"%s.cache",c->identity_path);
    if(load){int ok=load_game(g,path);pthread_mutex_lock(&c->mutex);
        ok=ok&&c->credentials_ready&&g->world.seed==c->seed;pthread_mutex_unlock(&c->mutex);return ok;}
    char temporary[560];snprintf(temporary,sizeof(temporary),"%s.tmp.XXXXXX",path);
    int fd=mkstemp(temporary);if(fd<0)return 0;close(fd);
    int ok=save_game(g,temporary);fd=open(temporary,O_RDONLY);
    if(fd<0)ok=0;else{if(fsync(fd))ok=0;if(close(fd))ok=0;}
    if(ok)ok=rename(temporary,path)==0;if(ok)ok=sync_parent(path);
    if(!ok)unlink(temporary);return ok;
}
static int enqueue(MpClient *c,int kind,const uint8_t *data,unsigned n) {
    if(c->out_count==QUEUE_CAP){c->queue_overflow++;return 0;}
    unsigned at=(c->out_head+c->out_count++)%QUEUE_CAP;c->out[at].bytes[0]=kind;memcpy(c->out[at].bytes+1,data,n);c->out[at].len=n+1;return 1;
}
void mp_pump(MpClient *c,Game *g) {
    if(!c)return;pthread_mutex_lock(&c->mutex);
    if(c->welcome_ready){game_new(g,c->seed,-1);c->welcome_ready=0;}
    if(c->state_ready) {
        State *s=malloc(sizeof(*s));
        if(s&&state_decode(s,c->state,c->state_n)) {
            if(!c->pose_ready){c->authority_start=s->player_pos;c->pose_ready=1;}
            c->authority_last=s->player_pos;
            int local_yaw=g->state.yaw;g->state=*s;g->state.yaw=local_yaw;g->substep=0;
            unsigned drop=0;while(drop<c->pending_n&&c->pending[drop].seq<=c->ack)drop++;
            memmove(c->pending,c->pending+drop,(c->pending_n-drop)*sizeof(Sample));c->pending_n-=drop;
            for(unsigned i=0;i<c->pending_n;i++){Sample *sample=&c->pending[i];g->state.yaw=sample->yaw;game_motion_tick(g,sample->input,20);}
            g->state.yaw=local_yaw;c->reconciliations++;
        }free(s);c->state_ready=0;
    }pthread_mutex_unlock(&c->mutex);
}
void mp_motion(MpClient *c,Game *g,Input input,unsigned ms) {
    if(!c||!mp_connected(c)){game_tick(g,input,ms);return;}
    pthread_mutex_lock(&c->mutex);c->remainder+=ms>250?250:ms;
    while(c->remainder>=20&&c->pending_n<QUEUE_CAP) {
        c->remainder-=20;uint8_t p[12];uint32_t seq=c->input_sequence+1;
        put32(p,seq);put16(p+4,(uint16_t)input.forward);put16(p+6,(uint16_t)input.strafe);
        put16(p+8,g->state.yaw);p[10]=input.jump;p[11]=input.run;
        if(!enqueue(c,3,p,sizeof(p)))break;
        c->input_sequence=seq;c->pending[c->pending_n++]=(Sample){seq,input,(uint16_t)g->state.yaw};
        game_motion_tick(g,input,20);input.jump=0;
    }pthread_mutex_unlock(&c->mutex);
}
int mp_action(MpClient *c,OpCode op,ItemId item,int amount,unsigned target) {
    if(!mp_connected(c))return 0;pthread_mutex_lock(&c->mutex);
    if(op==PICK_UP)target=item==IT_CABLE?1100u:1101u;
    if(op==REPAIR||op==DAMAGE)target=1200;
    unsigned i=0;while(i<6&&c->roles[i]!=target)i++;
    if(i==6){pthread_mutex_unlock(&c->mutex);return 0;}
    uint8_t p[56];put64(p,++c->action_sequence);put32(p+8,c->revision);put32(p+12,1);
    put16(p+16,op);put16(p+18,item);put32(p+20,(uint32_t)amount);memcpy(p+24,c->targets[i],16);memcpy(p+40,c->scope,16);
    if(c->journal_n==QUEUE_CAP){pthread_mutex_unlock(&c->mutex);return 0;}
    c->journal[c->journal_n]=(Outgoing){.len=57};c->journal[c->journal_n].bytes[0]=5;
    memcpy(c->journal[c->journal_n++].bytes+1,p,sizeof(p));
    int ok=journal_save(c);
    if(!ok){c->journal_n--;c->action_sequence--;}
    else enqueue(c,5,p,sizeof(p)); /* Durable queue retries even when mailbox is full. */
    pthread_mutex_unlock(&c->mutex);return ok;
}
int mp_offline_action(MpClient *c,Game *g,Operation operation) {
    /* A truly standalone save remains fully playable without this client.
     * Enrolled disconnected histories have a finite queue and explicit
     * vocabulary; reject shared mutations rather than quietly fork them. */
    if(!c||mp_connected(c)||operation.op!=CONDENSE||operation.item!=IT_CHIT)return 0;
    pthread_mutex_lock(&c->mutex);
    if(c->journal_n==QUEUE_CAP||!memcmp(c->device.bytes,(uint8_t[16]){0},16)){pthread_mutex_unlock(&c->mutex);return 0;}
    unsigned i=0;while(i<6&&c->roles[i]!=0)i++;
    Game previous=*g;
    if(i==6||!game_apply(g,operation)){pthread_mutex_unlock(&c->mutex);return 0;}
    Outgoing *out=&c->journal[c->journal_n++];memset(out,0,sizeof(*out));out->len=57;out->bytes[0]=7;
    uint8_t *p=out->bytes+1;put64(p,++c->action_sequence);put32(p+8,c->revision);put32(p+12,1);
    put16(p+16,operation.op);put16(p+18,operation.item);put32(p+20,operation.amount);
    memcpy(p+24,c->targets[i],16);memcpy(p+40,c->scope,16);
    int ok=journal_save(c);
    if(!ok){*g=previous;c->journal_n--;c->action_sequence--;}
    pthread_mutex_unlock(&c->mutex);return ok;
}
unsigned mp_peers(MpClient *c,MpPeer *out,unsigned cap) {
    if(!c)return 0;pthread_mutex_lock(&c->mutex);unsigned n=c->peer_n<cap?c->peer_n:cap;
    uint64_t stamp=now(),duration=c->snapshot_us-c->previous_us;
    float t=duration?(float)(stamp-c->snapshot_us)/duration:1;if(t>1)t=1;
    for(unsigned i=0;i<n;i++) {
        out[i]=c->peers[i];
        for(unsigned j=0;j<c->previous_n;j++)if(!memcmp(out[i].id.bytes,c->previous[j].id.bytes,16)){
            Pos a=c->previous[j].pos,b=out[i].pos;out[i].pos=(Pos){a.x+(int)((b.x-a.x)*t),a.y+(int)((b.y-a.y)*t),a.z+(int)((b.z-a.z)*t)};break;
        }
    }pthread_mutex_unlock(&c->mutex);return n;
}
void mp_report(MpClient *c,const char *path) {
    if(!c||!path)return;pthread_mutex_lock(&c->mutex);FILE *f=fopen(path,"w");if(f){
        fprintf(f,"{\"snapshots\":%llu,\"reconciliations\":%llu,\"connections\":%llu,\"peers_seen_max\":%u,\"bytes_sent\":%llu,\"bytes_received\":%llu,\"receipts\":%llu,\"successful_actions\":%llu,\"queue_overflow\":%u,\"authority_start_mm\":[%d,%d,%d],\"authority_last_mm\":[%d,%d,%d]}\n",
        (unsigned long long)c->snapshots,(unsigned long long)c->reconciliations,(unsigned long long)c->reconnects,c->peers_seen,
        (unsigned long long)c->sent,(unsigned long long)c->received,(unsigned long long)c->receipts,(unsigned long long)c->successful_actions,c->queue_overflow,
        c->authority_start.x,c->authority_start.y,c->authority_start.z,c->authority_last.x,c->authority_last.y,c->authority_last.z);fclose(f);
    }pthread_mutex_unlock(&c->mutex);
}
