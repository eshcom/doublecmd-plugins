-- install dependencies (if needed):
sudo apt install libgtk2.0-0 libgtk2.0-dev libgtkimageview0 libgtkimageview-dev

-- compile:
make
sudo mkdir -p /usr/lib/doublecmd/plugins/wlx/gtkimgview/
sudo cp gtkimgview.wlx /usr/lib/doublecmd/plugins/wlx/gtkimgview/
