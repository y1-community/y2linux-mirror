// SPDX-License-Identifier: GPL-2.0-only
/* cfg80211 is the regulatory authority. The legacy fullmac firmware receives
 * only its permitted HT20 station channels and per-channel power ceilings; no
 * country table from another product is used. Channels cfg80211 allows only
 * to listen on are given to the firmware too, so that it can receive there,
 * and are scanned in a pass of their own that sends nothing (y2_listen.h). */
#include "gl_os.h"
#include "precomp.h"
#include "y2_listen.h"
struct y2_regulation { u8 alpha2[2], allowed[14], listen[14]; s8 power[14]; };
/* One Wi-Fi device; written and read on the driver's main thread only. */
static struct y2_listen y2_listen;
/* What the firmware was last given: a beacon hint changes only which pass scans a channel, and
 * the firmware is not sent the same domain again in the middle of a scan. */
static struct y2_regulation y2_applied; static bool y2_applied_valid;
static WLAN_STATUS apply(P_ADAPTER_T adapter, PVOID buffer, UINT_32 size, PUINT_32 used)
{
	const struct y2_regulation *reg=buffer;
	P_GLUE_INFO_T glue=adapter->prGlueInfo;
	P_REG_INFO_T info=&glue->rRegInfo;
	CMD_SET_DOMAIN_INFO_T command={0};
	SET_TXPWR_CTRL_T power={0};
	unsigned group=0, last=0; WLAN_STATUS status;
	*used=sizeof(*reg);
	if (size!=sizeof(*reg)) return WLAN_STATUS_INVALID_LENGTH;
	if (y2_applied_valid && !memcmp(y2_applied.alpha2,reg->alpha2,sizeof(reg->alpha2)) &&
	    !memcmp(y2_applied.allowed,reg->allowed,sizeof(reg->allowed)) &&
	    !memcmp(y2_applied.power,reg->power,sizeof(reg->power))) {
		memcpy(y2_listen.channel,reg->listen,sizeof(y2_listen.channel));
		return WLAN_STATUS_SUCCESS;
	}
	y2_applied_valid=false;
	for (unsigned i=0;i<14;i++) {
		power.acTxPwrLimit2G[i]=reg->power[i];
		if (!reg->allowed[i]) { last=0; continue; }
		if (last && i==last) command.rSubBand[group-1].ucNumChannels++;
		else {
			if (group==MAX_SUBBAND_NUM) return WLAN_STATUS_INVALID_DATA;
			command.rSubBand[group++]=(CMD_SUBBAND_INFO){.ucRegClass=81,.ucBand=BAND_2G4,.ucChannelSpan=1,.ucFirstChannelNum=i+1,.ucNumChannels=1};
		}
		last=i+1;
	}
	if (!group) return WLAN_STATUS_INVALID_DATA;
	command.u2CountryCode=(reg->alpha2[0]<<8)|reg->alpha2[1];
	command.uc2G4Bandwidth=command.uc5GBandwidth=CONFIG_BW_20M;
	memcpy(y2_listen.channel,reg->listen,sizeof(y2_listen.channel));
	status=wlanSendSetQueryCmd(adapter,CMD_ID_SET_DOMAIN_INFO,TRUE,FALSE,FALSE,NULL,NULL,
		sizeof(command),(PUINT_8)&command,NULL,0);
	if (status!=WLAN_STATUS_PENDING) return status;
	info->eRegChannelListMap=REG_CH_MAP_CUSTOMIZED;
	for (unsigned i=0;i<MAX_SUBBAND_NUM;i++) {
        const CMD_SUBBAND_INFO *b=&command.rSubBand[i];
        info->rDomainInfo.rSubBand[i]=(DOMAIN_SUBBAND_INFO){b->ucRegClass,b->ucBand,b->ucChannelSpan,b->ucFirstChannelNum,b->ucNumChannels,0};
    }
	adapter->prDomainInfo=&info->rDomainInfo;
	adapter->rWifiVar.rConnSettings.u2CountryCode=command.u2CountryCode;
	glue->rTxPwr=power;
	status=wlanoidSetTxPower(adapter,&power,sizeof(power),used);
	if (status==WLAN_STATUS_SUCCESS || status==WLAN_STATUS_PENDING) { y2_applied=*reg; y2_applied_valid=true; }
	return status;
}
/* A new Wi-Fi device (every time the radio's firmware starts): it has been given nothing yet. */
void y2_wifi_reg_reset(void)
{
	memset(&y2_listen,0,sizeof(y2_listen));
	y2_applied_valid=false;
}
void y2_wifi_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request)
{
	struct y2_regulation reg={0};
	P_GLUE_INFO_T glue=wiphy_priv(wiphy);
	struct ieee80211_supported_band *band=wiphy->bands[NL80211_BAND_2GHZ];
	UINT_32 used; WLAN_STATUS ret;
	memcpy(reg.alpha2,request->alpha2,2);
	for (unsigned i=0;i<band->n_channels;i++) {
		struct ieee80211_channel *ch=&band->channels[i]; unsigned n=ch->hw_value;
		enum y2_channel_use use=y2_channel_use(n,!!(ch->flags & IEEE80211_CHAN_DISABLED),
			!!(ch->flags & IEEE80211_CHAN_RADAR),!!(ch->flags & IEEE80211_CHAN_NO_IR));
		if (use==Y2_CHANNEL_OFF) continue;
		/* The firmware may tune to both kinds; only the scan pass differs. */
		reg.allowed[n-1]=1;
		reg.listen[n-1]=use==Y2_CHANNEL_LISTEN;
		reg.power[n-1]=clamp_t(int,2*ch->max_power,-64,63);
	}
	ret=kalIoctl(glue,apply,&reg,sizeof(reg),FALSE,FALSE,TRUE,FALSE,&used);
	if (ret!=WLAN_STATUS_SUCCESS) {
		dev_err(glue->rHifInfo.Dev,"Wi-Fi regulatory setup failed: status=%#x\n",(u32)ret);
		y2_wifi_error(glue);
	}
}
/* Which channels are listen-only right now, from cfg80211's flags. Read before every scan: a
 * beacon heard on such a channel clears its flag (the beacon hint) without the notifier
 * running, and the firmware needs nothing new for it, since its domain has both kinds. */
static void y2_listen_refresh(P_ADAPTER_T adapter)
{
	struct ieee80211_supported_band *band=priv_to_wiphy(adapter->prGlueInfo)->bands[NL80211_BAND_2GHZ];
	u8 channel[14]={0};
	if (!band) return;
	for (unsigned i=0;i<band->n_channels;i++) {
		const struct ieee80211_channel *ch=&band->channels[i]; unsigned n=ch->hw_value;
		if (n>=1 && n<=14) channel[n-1]=y2_channel_use(n,!!(ch->flags & IEEE80211_CHAN_DISABLED),
			!!(ch->flags & IEEE80211_CHAN_RADAR),!!(ch->flags & IEEE80211_CHAN_NO_IR))==Y2_CHANNEL_LISTEN;
	}
	memcpy(y2_listen.channel,channel,sizeof(channel));
}
/* The channels of the scan request being built: the ordinary ones, sent on as the request
 * says, or in the listen pass the listen-only ones, received on only. */
unsigned y2_wifi_scan_channels(P_ADAPTER_T adapter, P_MSG_SCN_SCAN_REQ scan)
{
	RF_CHANNEL_INFO_T all[MAXIMUM_OPERATION_CHANNEL_LIST];
	UINT_8 total=0, count=0;
	if (!y2_listen.pass) y2_listen_refresh(adapter);
	rlmDomainGetChnlList(adapter,BAND_2G4,ARRAY_SIZE(all),&total,all);
	for (unsigned i=0;i<total && count<ARRAY_SIZE(scan->arChnlInfoList);i++)
		if (y2_listen_takes(&y2_listen,all[i].ucChannelNum)) scan->arChnlInfoList[count++]=all[i];
	scan->eScanChannel=SCAN_CHANNEL_SPECIFIED;
	scan->ucChannelListNum=count;
	if (y2_listen.pass) scan->eScanType=SCAN_TYPE_PASSIVE_SCAN;
	y2_listen.listened=y2_listen.pass;
	return count;
}
/* A scan pass of the station ended in `state`: start the listen pass when one follows (true:
 * the scan is not done yet). The state is entered again, which builds and sends the request. */
bool y2_wifi_scan_listen(P_ADAPTER_T adapter, ENUM_AIS_STATE_T state)
{
	bool sent;
	if (!y2_listen_follows(&y2_listen)) { y2_listen.listened=0; return false; }
	y2_listen.pass=1;
	aisFsmSteps(adapter,state);
	y2_listen.pass=0;
	/* Not sent (no memory): the scan ends with the ordinary pass. */
	sent=y2_listen.listened;
	if (!sent) y2_listen.listened=0;
	return sent;
}
