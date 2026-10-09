package com.nfsu2.androidevolved;

import android.content.Context;
import android.content.SharedPreferences;
import android.app.AlertDialog;
import android.widget.EditText;
import android.widget.Toast;
import org.json.JSONObject;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import java.util.ArrayList;
import java.util.HashMap;

/**
 * On-screen controls drawn as icons, in the style of the MW port: round buttons, pedals and a steering
 * stick, no captions. One surface tracks independent fingers so steering and pedals can be held together.
 */
final class RacingControlsView extends View {
    interface Keys { void set(int scan, boolean down); }
    // Special controls that do not map to a DirectInput scan code.
    private static final int MODE = -1, TEXT = -2, STICK = -3, EDIT = -5;
    private static final int ROUND = 0, PEDAL = 1, WHEEL = 2;
    private final Keys keys;
    private final Runnable textInput;
    private final Runnable configureTilt;
    private final Runnable configureFrameLimit;
    private String style;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private final ArrayList<Control> controls = new ArrayList<>();
    private final HashMap<Integer, Control> fingers = new HashMap<>();
    private boolean racing;
    private boolean lastGameMode;
    private boolean editing;
    private boolean hiddenByGamepad;
    private Control selected;
    private int dragPointer = -1;
    private float dragX, dragY;
    private final SharedPreferences layouts;
    private float scale;
    private static final int ACCENT = Color.rgb(255, 154, 50);
    private static final class Control {
        final RectF area;
        final String icon;
        final int scan, shape, color;
        final RectF original;
        String id;
        int held;
        float steering;
        Control(float x, float y, float w, float h, int shape, String icon, int scan, int color) {
            area = new RectF(x, y, x+w, y+h); original=new RectF(area); this.shape=shape; this.icon=icon; this.scan=scan; this.color=color;
        }
    }
    RacingControlsView(Context context, Keys keys, Runnable textInput, Runnable configureTilt, Runnable configureFrameLimit) {
        super(context); this.keys=keys; this.textInput=textInput;
        layouts=context.getSharedPreferences("touch_layouts_v1",Context.MODE_PRIVATE);
        style=layouts.getString("style","classic");this.configureTilt=configureTilt;this.configureFrameLimit=configureFrameLimit;
        setContentDescription("Controles del juego");
        setHapticFeedbackEnabled(true);
        line.setStyle(Paint.Style.STROKE);line.setStrokeCap(Paint.Cap.ROUND);line.setStrokeJoin(Paint.Join.ROUND);
        text.setTextAlign(Paint.Align.CENTER);text.setTypeface(Typeface.create("sans-serif-condensed",Typeface.BOLD));
    }
    private void add(float x, float y, float size, String icon, int scan) {add(x,y,size,size,ROUND,icon,scan,0);}
    private void add(float x, float y, float w, float h, int shape, String icon, int scan, int color) {
        Control c=new Control(x,y,w,h,shape,icon,scan,color);
        // v2: icon layout; positions saved for the old captioned buttons do not apply.
        c.id="v2."+(racing?"race."+(style.equals("classic")?"":style+"."):"menu.")+scan;
        float factor=layouts.getFloat(c.id+".size",1);
        float cx=layouts.getFloat(c.id+".x",c.area.centerX()/(getWidth()/scale))*(getWidth()/scale);
        float cy=layouts.getFloat(c.id+".y",c.area.centerY()/(getHeight()/scale))*(getHeight()/scale);
        c.area.set(cx-w*factor/2,cy-h*factor/2,cx+w*factor/2,cy+h*factor/2);
        clamp(c);controls.add(c);
    }
    private void clamp(Control c) {
        float w=getWidth()/scale,h=getHeight()/scale;
        float dx=c.area.left<4?4-c.area.left:c.area.right>w-4?w-4-c.area.right:0;
        float dy=c.area.top<4?4-c.area.top:c.area.bottom>h-4?h-4-c.area.bottom:0;
        c.area.offset(dx,dy);
    }
    @Override protected void onSizeChanged(int w,int h,int oldw,int oldh) { layoutControls(); }
    void setGameMode(boolean driving) {
        if(editing||lastGameMode==driving)return;
        lastGameMode=driving;racing=driving;layoutControls();
    }
    boolean isDriving() {return racing&&!editing&&!hiddenByGamepad;}
    /** A physical gamepad is connected: get out of the way until the screen is touched. */
    void setGamepadConnected(boolean connected) {
        if(hiddenByGamepad==connected||editing)return;
        hiddenByGamepad=connected;releaseAll();
    }
    private void layoutControls() {
        releaseAll(); controls.clear();selected=null;dragPointer=-1;
        if(getWidth()==0)return;
        scale=Math.min(getResources().getDisplayMetrics().density, getWidth()/820f);
        float w=getWidth()/scale, h=getHeight()/scale, bottom=h-20;
        add(w-62,16,46,"settings",EDIT);
        add(w-118,16,46,racing?"menu":"wheel",MODE);
        if(racing) {
            add(16,16,46,"pause",1);
            if(style.equals("classic")) {
                add(20,bottom-92,92,"left",0xcb);
                add(124,bottom-92,92,"right",0xcd);
                add(228,bottom-66,62,"handbrake",0x39);
                add(30,bottom-160,52,"shiftUp",0x2a);
                add(92,bottom-160,52,"shiftDown",0x1d);
                add(154,bottom-160,52,"camera",0x2e);
                add(w-110,bottom-150,90,150,PEDAL,"gas",0xc8,0xFF1E7A4A);
                add(w-206,bottom-108,82,108,PEDAL,"brake",0xd0,0xFF8A2A2A);
                add(w-290,bottom-72,70,"nitro",0x38);
            } else {
                boolean xbox=style.equals("xbox");
                add(24,bottom-160,160,160,WHEEL,"stick",STICK,0);
                add(w-106,bottom-150,86,150,PEDAL,"gas",0xc8,0xFF1E7A4A);
                add(w-198,bottom-108,78,108,PEDAL,"brake",0xd0,0xFF8A2A2A);
                add(w-252,bottom-178,58,xbox?"A":"×",0x39);
                add(w-186,bottom-238,58,xbox?"B":"○",0x38);
                add(w-318,bottom-238,58,xbox?"X":"□",0x2e);
                add(w-252,bottom-298,58,xbox?"Y":"△",0x30);
                add(30,bottom-228,52,"shiftUp",0x2a);
                add(92,bottom-228,52,"shiftDown",0x1d);
            }
        } else {
            float pad=60,cx=24+pad*1.5f,cy=bottom-pad*1.5f;
            add(cx-pad/2,cy-pad*1.5f,pad,"up",0xc8);
            add(cx-pad/2,cy+pad*.5f,pad,"down",0xd0);
            add(cx-pad*1.5f,cy-pad/2,pad,"left",0xcb);
            add(cx+pad*.5f,cy-pad/2,pad,"right",0xcd);
            add(w-96,bottom-110,76,"accept",0x1c);
            add(w-176,bottom-62,56,"back",1);
            add(w/2-23,bottom-46,46,"keyboard",TEXT);
        }
        invalidate();
    }

    // ---- Drawing -----------------------------------------------------------------------------------------

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if(hiddenByGamepad&&!editing)return;
        canvas.save(); canvas.scale(scale,scale);
        if(editing)canvas.drawColor(0x55000000);
        for(Control c:controls)drawControl(canvas,c);
        if(editing) {
            float w=getWidth()/scale;
            drawToolbarButton(canvas,new RectF(w/2-250,18,w/2-150,58),"RESTABLECER");
            drawToolbarButton(canvas,new RectF(w/2-142,18,w/2-102,58),"−");
            drawToolbarButton(canvas,new RectF(w/2-94,18,w/2-54,58),"+");
            drawToolbarButton(canvas,new RectF(w/2-46,18,w/2+58,58),"GUARDAR");
            drawToolbarButton(canvas,new RectF(w/2+66,18,w/2+170,58),"CANCELAR");
            drawToolbarButton(canvas,new RectF(w/2-65,70,w/2+65,106),"OPCIONES");
        }
        canvas.restore();
    }
    private void drawControl(Canvas canvas,Control c) {
        boolean on=c.held>0, picked=editing&&c==selected;
        float cx=c.area.centerX(),cy=c.area.centerY(),r=Math.min(c.area.width(),c.area.height())/2;
        int ink=on?ACCENT:0xE6FFFFFF;
        line.setStrokeWidth(Math.max(1.6f,r*.06f));
        if(c.shape==PEDAL) {
            float corner=c.area.width()*.28f;
            fill.setColor(on?0xD0FF9A32:(c.color&0x00FFFFFF)|0x99000000);
            canvas.drawRoundRect(c.area,corner,corner,fill);
            line.setColor(picked?0xFFFFCF69:on?0xFFFFD18A:0x99FFFFFF);
            canvas.drawRoundRect(c.area,corner,corner,line);
            // Grip ridges like a real pedal, then the direction it drives.
            line.setColor(on?0xFFFFE2B8:0x66FFFFFF);
            float span=c.area.width()*.28f;
            for(int i=0;i<4;++i){float y=c.area.top+c.area.height()*(.52f+i*.11f);canvas.drawLine(cx-span,y,cx+span,y,line);}
            line.setColor(ink);line.setStrokeWidth(Math.max(2.4f,c.area.width()*.06f));
            chevron(canvas,cx,c.area.top+c.area.height()*.27f,c.area.width()*.22f,c.icon.equals("gas"));
            return;
        }
        fill.setColor(0x55000000);canvas.drawCircle(cx,cy+r*.06f,r,fill);
        fill.setColor(on?0xD0FF9A32:0x8C141C26);canvas.drawCircle(cx,cy,r,fill);
        int ring=faceColor(c);
        line.setColor(picked?0xFFFFCF69:on?0xFFFFD18A:ring!=0?ring:0x8CFFFFFF);
        canvas.drawCircle(cx,cy,r-line.getStrokeWidth()/2,line);
        if(c.shape==WHEEL) {
            fill.setColor(on?ACCENT:0xCC3A4654);
            float knob=r*.4f,kx=cx+c.steering*(r-knob);
            canvas.drawCircle(kx,cy,knob,fill);line.setColor(0xB3FFFFFF);canvas.drawCircle(kx,cy,knob,line);
            line.setColor(0x80FFFFFF);chevronSide(canvas,cx-r*.78f,cy,r*.12f,true);chevronSide(canvas,cx+r*.78f,cy,r*.12f,false);
            return;
        }
        line.setColor(ink);fill.setColor(ink);line.setStrokeWidth(Math.max(2.2f,r*.1f));
        drawIcon(canvas,c,cx,cy,r,on);
    }
    private int faceColor(Control c) {
        if(!racing||style.equals("classic")||c.icon.length()!=1)return 0;
        if(c.scan==0x39)return style.equals("xbox")?0xff74df8e:0xff80b8ff;
        if(c.scan==0x38)return 0xffff7f86;
        if(c.scan==0x2e)return style.equals("xbox")?0xff80b8ff:0xffdd9cf4;
        return style.equals("xbox")?0xffffd778:0xff74dfb3;
    }
    private void drawIcon(Canvas canvas,Control c,float cx,float cy,float r,boolean on) {
        float s=r*.42f;
        switch(c.icon) {
            case "up": chevron(canvas,cx,cy,s,true);break;
            case "down": chevron(canvas,cx,cy,s,false);break;
            case "left": chevronSide(canvas,cx,cy,s,true);break;
            case "right": chevronSide(canvas,cx,cy,s,false);break;
            case "shiftUp": chevron(canvas,cx,cy-s*.3f,s*.8f,true);chevron(canvas,cx,cy+s*.35f,s*.8f,true);break;
            case "shiftDown": chevron(canvas,cx,cy-s*.35f,s*.8f,false);chevron(canvas,cx,cy+s*.3f,s*.8f,false);break;
            case "pause":
                canvas.drawRoundRect(cx-s*.6f,cy-s*.7f,cx-s*.18f,cy+s*.7f,s*.12f,s*.12f,fill);
                canvas.drawRoundRect(cx+s*.18f,cy-s*.7f,cx+s*.6f,cy+s*.7f,s*.12f,s*.12f,fill);break;
            case "settings":
                for(int i=-1;i<=1;++i){float y=cy+i*s*.62f,k=cx+(i==0?s*.35f:-s*.35f);
                    canvas.drawLine(cx-s*.85f,y,cx+s*.85f,y,line);canvas.drawCircle(k,y,s*.22f,fill);}break;
            case "menu":
                for(int i=0;i<4;++i){float x=cx+(i%2==0?-s*.5f:s*.5f),y=cy+(i<2?-s*.5f:s*.5f);
                    canvas.drawRoundRect(x-s*.36f,y-s*.36f,x+s*.36f,y+s*.36f,s*.1f,s*.1f,fill);}break;
            case "wheel":
                canvas.drawCircle(cx,cy,s*.9f,line);canvas.drawCircle(cx,cy,s*.2f,fill);
                canvas.drawLine(cx-s*.9f,cy,cx+s*.9f,cy,line);canvas.drawLine(cx,cy,cx,cy+s*.9f,line);break;
            case "camera":
                canvas.drawRoundRect(cx-s*.85f,cy-s*.45f,cx+s*.85f,cy+s*.6f,s*.2f,s*.2f,line);
                canvas.drawRect(cx-s*.35f,cy-s*.7f,cx+s*.1f,cy-s*.45f,fill);canvas.drawCircle(cx,cy+s*.08f,s*.3f,line);break;
            case "handbrake":
                canvas.drawCircle(cx,cy,s*.62f,line);
                canvas.drawArc(cx-s*.95f,cy-s*.95f,cx+s*.95f,cy+s*.95f,130,100,false,line);
                canvas.drawArc(cx-s*.95f,cy-s*.95f,cx+s*.95f,cy+s*.95f,-50,100,false,line);
                glyph(canvas,"P",cx,cy,s*.9f,on);break;
            case "nitro": glyph(canvas,"N₂O",cx,cy,s*.9f,on);break;
            case "accept":
                path.reset();path.moveTo(cx-s*.7f,cy);path.lineTo(cx-s*.15f,cy+s*.55f);path.lineTo(cx+s*.75f,cy-s*.5f);
                canvas.drawPath(path,line);break;
            case "back":
                canvas.drawLine(cx-s*.5f,cy-s*.5f,cx+s*.5f,cy+s*.5f,line);canvas.drawLine(cx+s*.5f,cy-s*.5f,cx-s*.5f,cy+s*.5f,line);break;
            case "keyboard":
                canvas.drawRoundRect(cx-s*.95f,cy-s*.6f,cx+s*.95f,cy+s*.6f,s*.15f,s*.15f,line);
                for(int row=0;row<2;++row)for(int col=0;col<4;++col)
                    canvas.drawCircle(cx-s*.54f+col*s*.36f,cy-s*.22f+row*s*.3f,s*.07f,fill);
                canvas.drawLine(cx-s*.4f,cy+s*.34f,cx+s*.4f,cy+s*.34f,line);break;
            default: {
                int color=faceColor(c);
                glyph(canvas,c.icon,cx,cy,r*(c.icon.length()==1?1.0f:.6f),on,color!=0&&!on?color:0);
            }
        }
    }
    private void chevron(Canvas canvas,float x,float y,float s,boolean up) {
        float d=up?-1:1;path.reset();path.moveTo(x-s,y-d*s*.45f);path.lineTo(x,y+d*s*.45f);path.lineTo(x+s,y-d*s*.45f);
        canvas.drawPath(path,line);
    }
    private void chevronSide(Canvas canvas,float x,float y,float s,boolean left) {
        float d=left?-1:1;path.reset();path.moveTo(x-d*s*.45f,y-s);path.lineTo(x+d*s*.45f,y);path.lineTo(x-d*s*.45f,y+s);
        canvas.drawPath(path,line);
    }
    private void glyph(Canvas canvas,String value,float x,float y,float size,boolean on) {glyph(canvas,value,x,y,size,on,0);}
    private void glyph(Canvas canvas,String value,float x,float y,float size,boolean on,int color) {
        text.setTextSize(size);text.setColor(color!=0?color:on?ACCENT:0xF0FFFFFF);
        canvas.drawText(value,x,y-(text.ascent()+text.descent())/2,text);
    }
    private void drawToolbarButton(Canvas canvas,RectF area,String label) {
        fill.setColor(0xE014202C);canvas.drawRoundRect(area,12,12,fill);
        text.setColor(ACCENT);text.setTextSize(label.length()>2?12:24);
        canvas.drawText(label,area.centerX(),area.centerY()-(text.ascent()+text.descent())/2,text);
    }

    // ---- Editor ------------------------------------------------------------------------------------------

    private void resizeSelected(float change) {
        if(selected==null)return;
        float factor=Math.max(.55f,Math.min(2.5f,selected.area.width()/selected.original.width()+change));
        float cx=selected.area.centerX(),cy=selected.area.centerY();
        float w=selected.original.width()*factor,h=selected.original.height()*factor;
        selected.area.set(cx-w/2,cy-h/2,cx+w/2,cy+h/2);clamp(selected);
    }
    private void saveLayout() {
        SharedPreferences.Editor save=layouts.edit();
        for(Control c:controls)save.putFloat(c.id+".x",c.area.centerX()/(getWidth()/scale))
                .putFloat(c.id+".y",c.area.centerY()/(getHeight()/scale))
                .putFloat(c.id+".size",c.area.width()/c.original.width());
        save.apply();
    }
    private boolean editTouch(MotionEvent e) {
        int action=e.getActionMasked(),index=e.getActionIndex();
        float x=e.getX(index)/scale,y=e.getY(index)/scale,w=getWidth()/scale;
        if(action==MotionEvent.ACTION_DOWN) {
            if(x>=w/2-65&&x<=w/2+65&&y>=70&&y<=106) {showOptions();return true;}
            if(y>=18&&y<=58) {
                boolean handled=true;
                if(x>=w/2-250&&x<=w/2-150) {for(Control c:controls)c.area.set(c.original);selected=null;}
                else if(x>=w/2-142&&x<=w/2-102)resizeSelected(-.1f);
                else if(x>=w/2-94&&x<=w/2-54)resizeSelected(.1f);
                else if(x>=w/2-46&&x<=w/2+58) {saveLayout();editing=false;layoutControls();}
                else if(x>=w/2+66&&x<=w/2+170) {editing=false;layoutControls();}
                else handled=false;
                if(handled) {invalidate();return true;}
            }
            selected=hit(e.getX(index),e.getY(index));
            dragPointer=e.getPointerId(index);dragX=x;dragY=y;
        } else if(action==MotionEvent.ACTION_MOVE&&selected!=null&&dragPointer>=0) {
            int i=e.findPointerIndex(dragPointer);
            if(i>=0) {x=e.getX(i)/scale;y=e.getY(i)/scale;selected.area.offset(x-dragX,y-dragY);clamp(selected);dragX=x;dragY=y;}
        } else if(action==MotionEvent.ACTION_CANCEL||action==MotionEvent.ACTION_UP||
                (action==MotionEvent.ACTION_POINTER_UP&&e.getPointerId(index)==dragPointer))dragPointer=-1;
        invalidate();return true;
    }
    private void showOptions() {
        new AlertDialog.Builder(getContext()).setTitle("Opciones de controles")
                .setItems(new String[]{"Estilo: clásico / Xbox / PlayStation","Conducción por inclinación","Límite de FPS","Guardar layout con nombre","Cargar layout personalizado"},(dialog,which)->{
                    if(which==0)chooseStyle();else if(which==1)configureTilt.run();
                    else if(which==2)configureFrameLimit.run();else if(which==3)saveNamedLayout();else loadNamedLayout();
                }).setNegativeButton("Cerrar",null).show();
    }
    private void chooseStyle() {
        String[] ids={"classic","xbox","playstation"};
        new AlertDialog.Builder(getContext()).setTitle("Estilo de conducción")
                .setSingleChoiceItems(new String[]{"Clásico · flechas y pedales","Xbox · volante y A / B / X / Y","PlayStation · volante y × / ○ / □ / △"},style.equals("classic")?0:style.equals("xbox")?1:2,(dialog,which)->{
                    style=ids[which];layouts.edit().putString("style",style).apply();
                    layoutControls();dialog.dismiss();
                }).setNegativeButton("Cancelar",null).show();
    }
    private void saveNamedLayout() {
        EditText name=new EditText(getContext());name.setSingleLine();name.setHint("Nombre del layout");
        name.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(24)});
        AlertDialog dialog=new AlertDialog.Builder(getContext()).setTitle("Guardar diseño de "+(racing?"conducción":"menú"))
                .setView(name).setPositiveButton("Guardar",null).setNegativeButton("Cancelar",null).create();
        dialog.setOnShowListener(ignored->dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v->{
            String value=name.getText().toString().trim();if(value.isEmpty()) {name.setError("Escribe un nombre");return;}
            try {
                JSONObject data=new JSONObject();data.put("style",style);
                for(Control c:controls) {JSONObject item=new JSONObject();
                    item.put("x",c.area.centerX()/(getWidth()/scale));item.put("y",c.area.centerY()/(getHeight()/scale));
                    item.put("size",c.area.width()/c.original.width());data.put(c.id,item);}
                layouts.edit().putString("preset."+(racing?"race.":"menu.")+value,data.toString()).apply();
                saveLayout();Toast.makeText(getContext(),"Layout guardado: "+value,Toast.LENGTH_SHORT).show();dialog.dismiss();
            }catch(org.json.JSONException exception) {name.setError("No se pudo guardar el layout");}
        }));dialog.show();
    }
    private void loadNamedLayout() {
        String prefix="preset."+(racing?"race.":"menu.");ArrayList<String> names=new ArrayList<>();
        for(String key:layouts.getAll().keySet())if(key.startsWith(prefix))names.add(key.substring(prefix.length()));
        java.util.Collections.sort(names);
        if(names.isEmpty()) {Toast.makeText(getContext(),"Todavía no hay layouts guardados para este modo",Toast.LENGTH_SHORT).show();return;}
        new AlertDialog.Builder(getContext()).setTitle("Cargar layout").setItems(names.toArray(new String[0]),(dialog,which)->{
            try {
                JSONObject data=new JSONObject(layouts.getString(prefix+names.get(which),"{}"));
                style=data.getString("style");SharedPreferences.Editor save=layouts.edit().putString("style",style);
                java.util.Iterator<String> ids=data.keys();while(ids.hasNext()) {String id=ids.next();if(id.equals("style"))continue;
                    JSONObject item=data.getJSONObject(id);save.putFloat(id+".x",(float)item.getDouble("x"))
                            .putFloat(id+".y",(float)item.getDouble("y")).putFloat(id+".size",(float)item.getDouble("size"));}
                save.apply();layoutControls();
            }catch(org.json.JSONException exception) {Toast.makeText(getContext(),"No se pudo cargar el layout",Toast.LENGTH_SHORT).show();}
        }).setNegativeButton("Cancelar",null).show();
    }

    // ---- Input -------------------------------------------------------------------------------------------

    /** The control under a point; round controls use their circle, with a little slack. */
    private Control hit(float x,float y) {
        x/=scale;y/=scale;Control best=null;double bestDistance=Double.MAX_VALUE;
        for(Control c:controls) {
            float cx=c.area.centerX(),cy=c.area.centerY();
            double d;
            if(c.shape==PEDAL) {
                if(Math.abs(x-cx)>c.area.width()*.56f||Math.abs(y-cy)>c.area.height()*.56f)continue;
                d=Math.hypot((x-cx)/c.area.width(),(y-cy)/c.area.height());
            } else {
                float r=Math.min(c.area.width(),c.area.height())/2;
                d=Math.hypot(x-cx,y-cy)/r;if(d>(c.shape==WHEEL?1.15:1.12))continue;
            }
            if(d<bestDistance){bestDistance=d;best=c;}
        }
        return best;
    }
    private void hold(Control c) {
        if(c==null||++c.held!=1)return;
        if(c.scan>=0)keys.set(c.scan,true);
        performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
    }
    private void release(Control c) { if(c!=null && --c.held==0) {
        if(c.scan>=0)keys.set(c.scan,false);
        else if(c.scan==STICK) {c.steering=0;keys.set(0xcb,false);keys.set(0xcd,false);}
    } }
    private void steer(Control c,float x) {
        c.steering=Math.max(-1,Math.min(1,(x/scale-c.area.centerX())/(c.area.width()/2)));
        keys.set(0xcb,c.steering<-.18f);keys.set(0xcd,c.steering>.18f);
    }
    void releaseAll() { for(Control c:fingers.values()) release(c); fingers.clear(); invalidate(); }
    @Override public boolean onTouchEvent(MotionEvent e) {
        if(editing)return editTouch(e);
        int action=e.getActionMasked(),index=e.getActionIndex(),id=e.getPointerId(index);
        if(hiddenByGamepad) {
            if(action==MotionEvent.ACTION_DOWN){hiddenByGamepad=false;invalidate();}
            return true;
        }
        if(action==MotionEvent.ACTION_CANCEL) { releaseAll(); return true; }
        if(action==MotionEvent.ACTION_DOWN||action==MotionEvent.ACTION_POINTER_DOWN) {
            Control c=hit(e.getX(index),e.getY(index));if(c!=null&&c.scan==STICK&&c.held>0)c=null;
            fingers.put(id,c);hold(c);if(c!=null&&c.scan==STICK)steer(c,e.getX(index));
        } else if(action==MotionEvent.ACTION_MOVE) {
            for(int i=0;i<e.getPointerCount();++i) {
                int pointer=e.getPointerId(i); Control old=fingers.get(pointer),next=hit(e.getX(i),e.getY(i));
                if(old!=null&&old.scan==STICK) {steer(old,e.getX(i));continue;}
                // Menu-style buttons do not slide; pedals and arrows follow the thumb.
                if(old!=null&&old.scan<0)continue;
                if(next!=null&&(next.scan==STICK&&next.held>0||next.scan<0&&next.scan!=STICK))next=null;
                if(old!=next) {release(old);fingers.put(pointer,next);hold(next);if(next!=null&&next.scan==STICK)steer(next,e.getX(i));}
            }
        } else if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_POINTER_UP) {
            Control c=fingers.remove(id); release(c);
            if(c!=null && c==hit(e.getX(index),e.getY(index))) {
                if(c.scan==MODE) {racing=!racing;layoutControls();}
                else if(c.scan==TEXT) {releaseAll();textInput.run();}
                else if(c.scan==EDIT) {releaseAll();editing=true;selected=null;}
                performClick();
            }
        }
        invalidate();return true;
    }
    @Override public boolean performClick() { super.performClick();return true; }
}
