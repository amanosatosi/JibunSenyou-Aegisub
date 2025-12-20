# aegisub toshi-ban v1.0

[English](https://github.com/amanosatosi/Aegisub_Toshi-ban) မြန်မာ

> *vibe-coded out of necessity*

aegisub version အသစ်ကိုတင်လိုက်တာ ဘာမှအသစ်လဲပါမလာတာတင်မက kfx template တွေပါပျက်ကုန်လို့စိတ်ညစ်နေသူများအတွက်
ကိုယ်တိုင်လုပ်ထားပေးတာပါ

---

## သွင်းနည်း

### Portable
1. release ထဲက zip ဖိုင်ကိုဒေါင်း
2. တခြားနေရာထဲမှာထည့်ထား
3. `aegisub.exe` ကိုနှစ်ချက် click

### Installer
1. release ထဲက installer ကိုဒေါင်း
2. zip ထဲကနေ installer ကိုထုတ်
3. သာမန်အတိုင်း install လိုက်

> portable version ထဲရပြီး installer version မှာမရဘူးဆို bug report လုပ်ပေးပါ
>  **Note:** **wx master** ဆိုပြီးနာမည်ထဲပါတာတွေက dark mode အစမ်းပါပါတယ် စမ်းကြည့်တာအရဆိုတော်တော်နီးပါးကကောင်းနေပြီ

---

## ပါလာတဲ့ feature ဘာညာ

### Subtitle editing
- **`\N` ကို `\N`လို့မဟုတ်ပဲ အကြောင်းအသစ်ဖြစ်အောင်လုပ်ထားတယ်
<img width="610" height="137" alt="image" src="https://github.com/user-attachments/assets/06dbeba9-0d97-4f71-b830-c787f3bc3657" />

- **k-timing လုပ်တုန်းနေရာဖြတ်ဖို့မေ့လို့ပြန်ဖြတ်တော့အကုန်အစကနေပြန်စရတာကိုမဖြစ်အောင်လုပ်ထားတယ်**

https://github.com/user-attachments/assets/3a9f5606-9d49-4984-84d1-de58a4e3c1d1

  
- **ဂျပန်ဂွင်း**လွယ်လွယ်ထည့်လို့ရအောင်လုပ်ထားတယ်
- <img width="390" height="366" alt="image" src="https://github.com/user-attachments/assets/165b7117-bd83-4f9c-92f5-a4c23c784df7" />

- **Join next**: နောက်စာကြောင်းက timing ကိုအခုသုံးနေတဲ့စာကြောင်းနဲ့ပေါင်းပြီး နောက်စာကြောင်းကစာတွေက original text box ထဲရောက်မယ်
- **Join last**: အရင်စာကြောင်းက timing ရောစာရောလက်ရှိစာကြောင်းနဲ့ပေါင်းတယ်
* **နေရာတချို့ပဲအရောင်ပြောင်းလို့ရအောင်လုပ်ထားတယ်**


https://github.com/user-attachments/assets/680a29a6-0b1d-4a98-990d-408ddb641011


* **VSfiltermod သမားတွေအတွက်** gradient tag တွေလွယ်လွယ်ထည့်လို့ရအောင်လုပ်ထားတယ်
  

https://github.com/user-attachments/assets/85be641b-4d56-47c1-9c09-e432611f32b9


* **`\t` ထဲ\c tag တွေထည့်လို့ရအောင်လုပ်ထားတယ်**

  > နဲနဲတော့အဆင်မပြေတာတို့ရှိပေမဲ့သုံးလို့တော့ရမယ်
- **actor section အတွက်**
- * တစ်ချက်ပဲနှိပ်တာနဲ့နာမည်အပြည့်ဖြည့်အောင်လုပ်ထားတယ်
* **Fast Naming Mode**
* ဒါကိုဖွင့်ရင်ဘာတွေရမလဲဆို
  * **အရင်အကြောင်းကနာမည်ကိုနောက်အကြောင်းဆီယူသယ်လာပေးမယ်**
  * **သုံးထားတဲ့နာမည်တွေကိုစာရင်းလုပ်ထားမယ်** ၁၀ခုအထိရပြီးအခုတော့ keyboard control နဲ့ပဲရပါသေးတယ်
  * **Nanashi mode (option ထဲပြောင်းလို့ရ)**: Ctrl နှိပ်ရင် enter လိုမျိုးသုံးလို့ရပြီး alt နှိပ်ရင်အရင်အကြောင်းဘက်ကိုပြန်မယ်

သုံးချင်တဲ့သူတွေစိတ်ဝင်စားရင်ကိုယ့် script **Irodori** ကိုပါတစ်ချက်ကြည့်လိငုက်ပါဦး

---

### Style နဲ့ဘာညာ

* **style ထည့်တဲ့အချိန်ကြ**:

  * ထည့်မဲ့စာဖိုင်က resolution မတူရင်သတိပေးမယ်
  * နာမည်တူတာများနေရင်တစ်ချက်တည်းနဲ့အကုန် replace လို့ရအောင်လုပ်ထားပေးတယ်

* **Relative time ကို click ရင်** အဲ့က data ကို auto copy ပေးတာဒါမှမဟုတ်တစ်ခါတည်းထည့်ပေးတာပါထည့်‌ပေးထားတယ်

* **effect field ထဲ** `LOCK` (အကုန်အကြီး)ထည့်ရင်နေရာပြောင်းတာဘာညာမှာပေါ်မလာအောင်လုပ်ထားတယ်

* **Force Zoom** စာဖိုင်ထဲက video zoom data ကိုအဖက်မလုပ်ပဲကိုယ့် default zoom နဲ့ video ဖွင့်ပေးတယ်
---

**alt c နဲ့ comment ကြသူများအတွက်**

* **Alt + C → Edit / Line / Comment** ဆိုတဲ့ shortcut ထည့်လိုက်ပါ

  > အဲ့တာသုံးရင် စာပြင်နေတုန်းမှာ mouse နဲ့ပြန်ပြီး edit box ဆီ click စရာမလိုဘူး
* **Edit / Line / Comment / Invert** ဆိုတာလဲရှိတယ်နော်

  > အကုန်လုံးကို comment/uncomment လုပ်တာမဟုတ်ပဲ comment ထားရင် dialog ၊ dialog ဆို comment ပြောင်းပေးတယ်
  
> ⚠️ Note  မသုံးရင်လဲ alt + c ကိုတခြား command အတွက်သုံးလို့ရ

---

### Script ဘာညာ

- **အရင်က template တွေမှာ error တက်တာပြင်ထား**
* **[Dependency control configure script](https://github.com/garret1317/aegisub-scripts/blob/master/garret.depctrl_config.lua)** (from [garret1317](https://github.com/garret1317)様)

  > dependency control သုံးရင် auto update တွေရှိတယ် ဒါနဲ့မှပိတ်လို့ရတယ် မပိတ်ပဲထားထားရင်တစ်ခါ update ရင်၁၀မိနစ်လောက်ကြာရော

---
### video အတွက်
- **4x speed အထိမြန်အောင်လုပ်လို့ရတယ် 0.25 အထိနှေးလို့လဲရတယ်** 
- **အသံဖိုင်မှာ ဘာသာစကားနာမည်ဘာညာတွေကြည့်လို့ရအောင်လုပ်ထားတယ်**
<img width="444" height="240" alt="image" src="https://github.com/user-attachments/assets/75173d12-58c0-4d70-ab84-3360922ae665" />

---

### UI
- dark mode အစမ်းနဲ့အဲ့တာအတွက် icon အသစ်တွေထည့်ထားတယ်

---

## Themes

* [sgt0](https://github.com/sgt0/aegisub-themes) ကလုပ်ထားတဲ့ theme colection နဲ့ကိုယ်လုပ်ထားတဲ့ dark theme(unofficial) ပါတယ်
---

## Theme credits

theme တွေလုပ်ထားတဲ့မူရင်း creator များ

* **Ayu (Light / Dark / Mirage)** — dempfi
  [https://github.com/dempfi/ayu](https://github.com/dempfi/ayu)

* **Catppuccin (Latte / Mocha)** — Catppuccin Project
  [https://github.com/catppuccin/catppuccin](https://github.com/catppuccin/catppuccin)

* **Dracula** — The Dracula Theme Team
  [https://github.com/dracula/dracula-theme](https://github.com/dracula/dracula-theme)

* **Solarized (Dark / Light)** — Ethan Schoonover
  [https://ethanschoonover.com/solarized](https://ethanschoonover.com/solarized)

* **Zenburn** — Jani Nurminen
  [https://github.com/jnurmine/Zenburn](https://github.com/jnurmine/Zenburn)

* **Seti** — Jesse Weed
  [https://github.com/jesseweed/seti-ui](https://github.com/jesseweed/seti-ui)

* **Sakura** — Misterio77
  [https://github.com/Misterio77/base16-sakura-scheme](https://github.com/Misterio77/base16-sakura-scheme)

* **Mountain** — gnsfujiwara
  [https://github.com/tinted-theming/base16-schemes/blob/main/mountain.yaml](https://github.com/tinted-theming/base16-schemes/blob/main/mountain.yaml)

---

**Thanks to sgt0** for collecting and maintaining a public Aegisub theme bundle, which made including these themes possible:
[https://github.com/sgt0/aegisub-themes](https://github.com/sgt0/aegisub-themes)

> Theme authorship belongs to their respective creators.
> sgt0 is credited for assembling and distributing the theme collection.

---

 ##arch1t3ct’s fork ကိုအခြေခံထားလို့သူ့ထဲက

 * Video panning
 * Line folding
 * Perspective tool
 * Lua API အသစ်
 * Stereo audio
တွေရောဒီထဲမှာပါပါတယ်


---

## Bug တွေသတင်းပို့ဖို့နဲ့အထင်အမြင်များပြောဖို့

- GitHub Issues: [Link](https://github.com/amanosatosi/JibunSenyou-Aegisub/issues/new/choose)
- Telegram: ([link](https://t.me/waipuro))
---

## နောက် update နဲ့ပတ်သတ်ပြီး

နောက် update **၂** ခုပဲလုပ်ဖို့ကြံထားပါတယ်
bug တွေတွေ့ကြရင်အဲ့တာကိုပြင်ဖို့အတွက် update နဲ့
wxWidgets 3.3.2 ထွက်ရင် dark mode ပိုကောင်းစေဖို့ update တစ်ခုပါပဲ

ဒီထက်ပိုပြီးတော့မလုပ်နိုင်တော့ဘူး



## Who this is for

This fork is for people who actually use Aegisub.  
The priority is simple: **works well, feels good, wastes less time**.
