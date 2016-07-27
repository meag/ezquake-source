#!/bin/bash

set -e

# Delete existing asset
#   FIXME: to prevent data loss, upload with unique name, delete old, rename new?
function gitDeleteAsset #(assetId, authentication)
{
	curl -u "$2:" -# -XDELETE $APIURL/assets/$1 -o misc/travis-build/.deleteasset.json
}

# Upload an asset to a release (if asset exists, will be overwritten)
#   FIXME: curl should --data-urlencode the name, but POST the file
function gitUploadAsset #(releaseId, assetName, path, authentication)
{
	# Find existing asset with the same name
	ASSET_ID=`cat misc/travis-build/.release.json | jq -c -e '.assets[]? | { id: .id, name: .name }' | grep $2 | jq -r '.id'` || true
	if [ "$ASSET_ID" ]; then
		gitDeleteAsset $ASSET_ID, $4
	fi

	echo "Uploading asset $3"
	curl -u "$4:" -# -XPOST -H Content-Type:application/octet-stream --data-binary @$3 "$UPLOADURL/$1/assets?name=$2" -o misc/travis-build/.uploaded.json
}

# Don't deploy for builds triggered by pull requests
if [ "$TRAVIS_PULL_REQUEST" != "false" ]; then
	echo "Pull request detected - simple build."
	make
	exit 0
fi

# repo address
GITURL=`git config remote.origin.url`
APIURL=${GITURL//github.com/api.github.com\/repos}
APIURL=${APIURL%.git}/releases
UPLOADURL=${APIURL//api.github.com/uploads.github.com}

# Find corresponding release: default to latest | latest-<branch>
TAGNAME=$TRAVIS_TAG
if [ ! $TAGNAME ]; then
	if [ "$TRAVIS_BRANCH" != "master" ]; then
		TAGNAME="latest-$TRAVIS_BRANCH"
	else
		TAGNAME="latest"
	fi
fi

ARCHIVEFILE=ezquake-$AUTOBUILD_SUFFIX-$TAGNAME

# Find release details from github
echo "Looking for tag $APIURL/tags/$TAGNAME"
curl -u "$AUTHTOKEN:" "$APIURL/tags/$TAGNAME" -o misc/travis-build/.release.json
RELEASE_ID=`cat misc/travis-build/.release.json | jq -r '.id'` || true
if [ "$RELEASE_ID" = "null" -o ! "$RELEASE_ID" ]; then
	echo "No release called '$TAGNAME' - performing simple build"
	make
	exit 0
fi

SHA=`git rev-parse --verify HEAD`

# ezQuake uses number of commits for versioning: travis uses --depth 50, so we need to get the rest
git fetch --unshallow || true   # remove || true in production

if [ "$TRAVIS_OS_NAME" = "osx" ]; then
    make strip
    echo "Creating OSX archive..."
    sh misc/install/create_osx_bundle.sh
    mv ezquake.zip $ARCHIVEFILE.zip
    ARCHIVEFILE=$ARCHIVEFILE.zip
elif [ "$EZ_CONFIG_FILE" == ".config_windows" ]; then
    make strip
    echo "Creating Windows archive..."
    zip $ARCHIVEFILE.zip ezquake.exe LICENSE
    ARCHIVEFILE=$ARCHIVEFILE.zip
else
    make strip
    echo "Creating Linux archive..."
    ARCHIVEFILE=$ARCHIVEFILE.tar
    tar -cvf $ARCHIVEFILE ezquake-*-*
    tar -rf $ARCHIVEFILE LICENSE
    gzip $ARCHIVEFILE
    ARCHIVEFILE=$ARCHIVEFILE.gz
fi

# Deploy to github releases...
gitUploadAsset "$RELEASE_ID" "$ARCHIVEFILE" "$ARCHIVEFILE" "$AUTHTOKEN"

# TODO: if a tagged release, create -full.zip etc and upload those too

# tidy up
rm $ARCHIVEFILE
