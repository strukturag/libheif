function StringToArrayBuffer(str) {
    var buf = new ArrayBuffer(str.length);
    var bufView = new Uint8Array(buf);
    for (var i=0, strLen=str.length; i<strLen; i++) {
        bufView[i] = str.charCodeAt(i);
    }
    return buf;
}

var HeifImage = function(handle) {
    this.handle = handle;
    this.img = null;
};

HeifImage.prototype.free = function() {
    if (this.handle) {
        Module.heif_image_handle_release(this.handle);
        this.handle = null;
    }
};

HeifImage.prototype._ensureImage = function() {
    if (this.img) {
        return;
    }

    var img = Module.heif_js_decode_image(this.handle,
      Module.heif_colorspace.heif_colorspace_YCbCr, Module.heif_chroma.heif_chroma_420);
    if (!img || img.code) {
        console.log("Decoding image failed", this.handle, img);
        return;
    }

    this.data = new Uint8Array(StringToArrayBuffer(img.data));
    delete img.data;
    this.img = img;

    if (img.alpha !== undefined) {
        this.alpha = new Uint8Array(StringToArrayBuffer(img.alpha));
        delete img.alpha;
    }
};

HeifImage.prototype.get_width = function() {
    return Module.heif_image_handle_get_width(this.handle);
};

HeifImage.prototype.get_height = function() {
    return Module.heif_image_handle_get_height(this.handle);
};

HeifImage.prototype.is_primary = function() {
    return !!Module.heif_image_handle_is_primary_image(this.handle);
}

HeifImage.prototype.has_alpha_channel = function() {
    return !!Module.heif_image_handle_has_alpha_channel(this.handle);
}

HeifImage.prototype.is_premultiplied_alpha = function() {
    return !!Module.heif_image_handle_is_premultiplied_alpha(this.handle);
}

HeifImage.prototype.display = function(image_data, callback) {
    // Defer color conversion.
    var w = this.get_width();
    var h = this.get_height();

    setTimeout(function() {

        // If image hasn't been loaded yet, decode the image

        if (!this.img) {
            var img = Module.heif_js_decode_image2(this.handle,
              Module.heif_colorspace.heif_colorspace_RGB, Module.heif_chroma.heif_chroma_interleaved_RGBA);
            if (!img || img.code) {
                console.log("Decoding image failed", this.handle, img);

                callback(null);
                return;
            }

            for (let c of img.channels) {
                if (c.id == Module.heif_channel.heif_channel_interleaved) {

                    // copy image into output array

                    if (c.stride == c.width * 4) {
                        image_data.data.set(c.data);
                    } else {
                        for (let y = 0; y < c.height; y++) {
                            let slice = c.data.slice(y * c.stride, y * c.stride + c.width * 4);
                            let offset = y * c.width * 4;
                            image_data.data.set(slice, offset);
                        }
                    }
                }
            }

            Module.heif_image_release(img.image);
        }

        callback(image_data);
    }.bind(this), 0);
};

var HeifDecoder = function() {
    this.decoder = null;
};

HeifDecoder.prototype.decode = function(buffer) {
    if (this.decoder) {
        Module.heif_context_free(this.decoder);
    }
    this.decoder = Module.heif_context_alloc();
    if (!this.decoder) {
        console.log("Could not create HEIF context");
        return [];
    }
    var error = Module.heif_context_read_from_memory(this.decoder, buffer);
    if (error.code !== Module.heif_error_code.heif_error_Ok) {
        console.log("Could not parse HEIF file", error.message);
        return [];
    }

    var ids = Module.heif_js_context_get_list_of_top_level_image_IDs(this.decoder);
    if (!ids || ids.code) {
        console.log("Error loading image ids", ids);
        return [];
    } else if (!ids.length) {
        console.log("No images found");
        return [];
    }

    var result = [];
    for (var i = 0; i < ids.length; i++) {
        var handle = Module.heif_js_context_get_image_handle(this.decoder, ids[i]);
        if (!handle || handle.code) {
            console.log("Could not get image data for id", ids[i], handle);
            continue;
        }

        result.push(new HeifImage(handle));
    }
    return result;
};

var fourcc = function(s) {
    return s.charCodeAt(0) << 24 |
      s.charCodeAt(1) << 16 |
      s.charCodeAt(2) << 8 |
      s.charCodeAt(3);
}

Module.HeifImage = HeifImage;
Module.HeifDecoder = HeifDecoder;
Module.fourcc = fourcc;

// Expose enum values.
const enums = [
    'heif_error_code',
    'heif_suberror_code',
    'heif_compression_format',
    'heif_chroma',
    'heif_colorspace',
    'heif_channel'
];
for (const e of enums) {
    for (const key in Module[e]) {
        if (!Module[e].hasOwnProperty(key) || key === 'values') {
            continue;
        }
        Module[key] = Module[e][key];
    }
}

// Expose internal C API.
for (const key in Module) {
    if (key.indexOf('_heif_') !== 0 || Module[key.slice(1)] !== undefined) {
        continue;
    }
    Module[key.slice(1)] = Module[key];
}
