#! /bin/sh

# This script creates a multi vib component for VMware ESXi7.0
# The script shall create a directory componentCreate in /tmp
# If the directory exists, it contents shall be over written.
# The scripts expects one primary vib component and multiple 
# secondary vib components

helpUsage()
{
    echo "Script Usage:"
    echo "./createComponent.sh --primary <Primary_Offline_Bundle.zip>
          --secondary <Secondary_1_Offline_Bundle.zip> <Secondary_2_Offline_Bundle.zip>
                  <Secondary_3_Offline_Bundle.zip>"
    echo "The script is to be executed on a VMWare Workbench machine and is applicable for Esxi 7.0."
    exit
}


extractMetadata()
{
    unzip -q $1
    unzip -q metadata.zip -d metadata
}

extractPrimaryMetadata()
{
    extractMetadata $1
    primaryBulletinFile=`ls $PRIMARYDIR/metadata/bulletins/`
    componentFile=`echo $primaryBulletinFile | sed -e 's/xml/zip/g'`
    primaryBulletinFile="$PRIMARYDIR/metadata/bulletins/$primaryBulletinFile"
}

addVibVmware()
{
    vibPrimaryStartPos=`grep -n  "</vibList>" "$PRIMARYDIR"/metadata/vmware.xml | cut -d ":" -f1`
    vibPrimaryStartPos=$(($vibPrimaryStartPos-1))
    vibData=`sed -n '/<vib>/,/<\/vib>/p;/<\/vib>/q' metadata/vmware.xml`
    vibData=$(echo $vibData | sed -e "s/\"/\\\"/g")
    sed -i "${vibPrimaryStartPos} a $vibData"  "$PRIMARYDIR"/metadata/vmware.xml
}

addVibIdBulletin()
{
    data=`cat $secondaryBulletinFile | sed 's/.*<vibList>\(.*\)<\/vibList>.*/\1/'`
    data=$(echo $data| sed -e 's/\/vibID/\\\/vibID/g')
    sed -i "s/<\/vibID>/<\/vibID>$data/$1g" $primaryBulletinFile
}

processSecondaryVib()
{
    if [[ -d $SECONDARYDIR ]]
    then
        rm -rf $SECONDARYDIR
    fi
    #echo "Creating Secondary Dir for $1"
    mkdir -p $SECONDARYDIR
    cd $SECONDARYDIR
    extractMetadata $1
    cp -r "$SECONDARYDIR/metadata/vibs/"* "$PRIMARYDIR/metadata/vibs"
    addVibVmware
    secondaryBulletinFile=`ls $SECONDARYDIR/metadata/bulletins/`
    secondaryBulletinFile="$SECONDARYDIR/metadata/bulletins/$secondaryBulletinFile"
    addVibIdBulletin $2
    cp -r "$SECONDARYDIR/vib20/"* "$PRIMARYDIR/vib20"
}


for arg in "$@"
do
    case $arg in
	-p|--primary)
             PRIMARYVIB="$2"
             shift
             shift
        ;;
	-s|--secondary)
             shift
             i=0
             declare -a secondaryVIBArr
             while [ $# -gt 0 ]
             do
                 secondaryVIBArr[i]="$1"
                 shift
                 i=$(($i+1))
             done
	;;
        -h|--help)
            helpUsage
	;;
    esac
done

if [[ ! -e $PRIMARYVIB ]]
then
    echo "Error: $PRIMARYVIB path not found. Exiting"
    helpUsage
fi
if [ ${PRIMARYVIB: -4} != ".zip" ]
then
    echo "Error: $PRIMARYVIB not a zip file Exiting"
    helpUsage
fi

for secondaryFile in "${secondaryVIBArr[@]}"
do
    if [[ ! -e $secondaryFile ]]
    then
        echo "Error: $secondaryFile path not found. Exiting"
        helpUsage
    fi
    if [ ${secondaryFile: -4} != ".zip" ]
    then
        echo "Error: $secondaryFile not a zip file Exiting"
        helpUsage
    fi
done

WORK_BASEDIR=/tmp/componentCreate/

if [[ ! -d $WORK_BASEDIR ]]
then
   echo "Creating $WORK_BASEDIR"
    mkdir -p $WORK_BASEDIR
else
   echo "Deleting contents of $WORK_BASEDIR"
    rm -rf "$WORK_BASEDIR"/*
fi

STAGEDIR="$WORK_BASEDIR"/stagedir
PRIMARYDIR="$STAGEDIR"/primary
SECONDARYDIR="$STAGEDIR"/secondary
mkdir -p $PRIMARYDIR
cd  $PRIMARYDIR
extractPrimaryMetadata $PRIMARYVIB
index=1
for secondaryFile in "${secondaryVIBArr[@]}"
do
    processSecondaryVib $secondaryFile $index
    index=$(($index+1))
done

VIBPUBLISHDIR="$WORK_BASEDIR"/stage
if [[ -d $VIBPUBLISHDIR ]]
then
    rm -rf "VIBPUBLISHDIR"/*
else
    echo "Creating $VIBPUBLISHDIR"
    mkdir -p $VIBPUBLISHDIR
fi

cd $PRIMARYDIR
rm -rf metadata.zip
VENDOR_CODE=SFC
VENDOR_NAME=Solarflare

vibpublish -g -d metadata/bulletins/ -s vib20/ -t ESXi,7.0.0 -e $componentFile -o $VIBPUBLISHDIR --vendor-code $VENDOR_CODE -n $VENDOR_NAME
if [ $? -eq 0 ]
then
 echo "Multi VIB Component file $componentFile Created Successfully at $PRIMARYDIR"
else
 echo "Component Creation failed!!"
fi
