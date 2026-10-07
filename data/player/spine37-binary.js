/*
 * Spine 3.7 binary skeleton reader for the spine-ts 3.7 runtime.
 *
 * spine-ts 3.7 only ships SkeletonJson. This is a port of the official
 * spine-libgdx 3.7.94 SkeletonBinary reader that builds the same spine-ts
 * objects as SkeletonJson, so the bundled 3.7 runtime can play .skel exports.
 * Derived from the Spine Runtimes; see runtime/SPINE-RUNTIMES-LICENSE.txt.
 */
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  root.SpineBinary37 = api;
  if (root.spine && !root.spine.SkeletonBinary) api.install(root.spine);
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  const BONE_ROTATE = 0;
  const BONE_TRANSLATE = 1;
  const BONE_SCALE = 2;
  const BONE_SHEAR = 3;
  const SLOT_ATTACHMENT = 0;
  const SLOT_COLOR = 1;
  const SLOT_TWO_COLOR = 2;
  const PATH_POSITION = 0;
  const PATH_SPACING = 1;
  const PATH_MIX = 2;
  const CURVE_STEPPED = 1;
  const CURVE_BEZIER = 2;
  const ATTACHMENT_TYPES = ['region', 'boundingbox', 'mesh', 'linkedmesh', 'path', 'point', 'clipping'];

  class BinaryInput {
    constructor(data) {
      this.bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
      this.view = new DataView(this.bytes.buffer, this.bytes.byteOffset, this.bytes.byteLength);
      this.position = 0;
    }

    require(count) {
      if (this.position + count > this.bytes.length) throw new Error('Unexpected end of Spine binary data');
    }

    readByte() {
      this.require(1);
      return this.view.getInt8(this.position++);
    }

    readUnsignedByte() {
      this.require(1);
      return this.bytes[this.position++];
    }

    readShort() {
      this.require(2);
      const value = this.view.getInt16(this.position);
      this.position += 2;
      return value;
    }

    readInt32() {
      this.require(4);
      const value = this.view.getInt32(this.position);
      this.position += 4;
      return value;
    }

    readInt(optimizePositive) {
      let b = this.readUnsignedByte();
      let result = b & 0x7f;
      if (b & 0x80) {
        b = this.readUnsignedByte();
        result |= (b & 0x7f) << 7;
        if (b & 0x80) {
          b = this.readUnsignedByte();
          result |= (b & 0x7f) << 14;
          if (b & 0x80) {
            b = this.readUnsignedByte();
            result |= (b & 0x7f) << 21;
            if (b & 0x80) {
              b = this.readUnsignedByte();
              result |= (b & 0x7f) << 28;
            }
          }
        }
      }
      // Java ints: an optimized-positive varint can still encode a negative draw order offset.
      return optimizePositive ? result | 0 : (result >>> 1) ^ -(result & 1);
    }

    readString() {
      let byteCount = this.readInt(true);
      if (byteCount === 0) return null;
      if (byteCount === 1) return '';
      byteCount--;
      let chars = '';
      for (let index = 0; index < byteCount;) {
        const b = this.readUnsignedByte();
        switch (b >> 4) {
          case 12:
          case 13:
            chars += String.fromCharCode(((b & 0x1f) << 6) | (this.readUnsignedByte() & 0x3f));
            index += 2;
            break;
          case 14:
            chars += String.fromCharCode(
              ((b & 0x0f) << 12) | ((this.readUnsignedByte() & 0x3f) << 6) | (this.readUnsignedByte() & 0x3f)
            );
            index += 3;
            break;
          default:
            chars += String.fromCharCode(b);
            index++;
        }
      }
      return chars;
    }

    readFloat() {
      this.require(4);
      const value = this.view.getFloat32(this.position);
      this.position += 4;
      return value;
    }

    readBoolean() {
      return this.readByte() !== 0;
    }
  }

  function readHeader(input) {
    const hash = input.readString();
    const version = input.readString();
    return { hash, version };
  }

  function install(spine) {
    function rgba8888(color, value) {
      color.r = ((value & 0xff000000) >>> 24) / 255;
      color.g = ((value & 0x00ff0000) >>> 16) / 255;
      color.b = ((value & 0x0000ff00) >>> 8) / 255;
      color.a = (value & 0x000000ff) / 255;
      return color;
    }

    function rgb888(color, value) {
      color.r = ((value & 0x00ff0000) >>> 16) / 255;
      color.g = ((value & 0x0000ff00) >>> 8) / 255;
      color.b = (value & 0x000000ff) / 255;
      return color;
    }

    class LinkedMesh {
      constructor(mesh, skin, slotIndex, parent) {
        this.mesh = mesh;
        this.skin = skin;
        this.slotIndex = slotIndex;
        this.parent = parent;
      }
    }

    class SkeletonBinary {
      constructor(attachmentLoader) {
        if (!attachmentLoader) throw new Error('attachmentLoader cannot be null.');
        this.attachmentLoader = attachmentLoader;
        this.scale = 1;
        this.linkedMeshes = [];
      }

      readSkeletonData(binary) {
        const scale = this.scale;
        const skeletonData = new spine.SkeletonData();
        const input = new BinaryInput(binary);

        const header = readHeader(input);
        skeletonData.hash = header.hash || null;
        skeletonData.version = header.version || null;
        skeletonData.width = input.readFloat();
        skeletonData.height = input.readFloat();

        const nonessential = input.readBoolean();
        if (nonessential) {
          skeletonData.fps = input.readFloat();
          skeletonData.imagesPath = input.readString() || null;
          skeletonData.audioPath = input.readString() || null;
        }

        for (let i = 0, n = input.readInt(true); i < n; i++) {
          const name = input.readString();
          const parent = i === 0 ? null : skeletonData.bones[input.readInt(true)];
          const data = new spine.BoneData(i, name, parent);
          data.rotation = input.readFloat();
          data.x = input.readFloat() * scale;
          data.y = input.readFloat() * scale;
          data.scaleX = input.readFloat();
          data.scaleY = input.readFloat();
          data.shearX = input.readFloat();
          data.shearY = input.readFloat();
          data.length = input.readFloat() * scale;
          data.transformMode = input.readInt(true);
          if (nonessential) input.readInt32();
          skeletonData.bones.push(data);
        }

        for (let i = 0, n = input.readInt(true); i < n; i++) {
          const slotName = input.readString();
          const boneData = skeletonData.bones[input.readInt(true)];
          const data = new spine.SlotData(i, slotName, boneData);
          rgba8888(data.color, input.readInt32());
          const darkColor = input.readInt32();
          if (darkColor !== -1) data.darkColor = rgb888(new spine.Color(1, 1, 1, 1), darkColor);
          data.attachmentName = input.readString();
          data.blendMode = input.readInt(true);
          skeletonData.slots.push(data);
        }

        for (let i = 0, n = input.readInt(true); i < n; i++) {
          const data = new spine.IkConstraintData(input.readString());
          data.order = input.readInt(true);
          for (let ii = 0, nn = input.readInt(true); ii < nn; ii++) data.bones.push(skeletonData.bones[input.readInt(true)]);
          data.target = skeletonData.bones[input.readInt(true)];
          data.mix = input.readFloat();
          data.bendDirection = input.readByte();
          data.compress = input.readBoolean();
          data.stretch = input.readBoolean();
          data.uniform = input.readBoolean();
          skeletonData.ikConstraints.push(data);
        }

        for (let i = 0, n = input.readInt(true); i < n; i++) {
          const data = new spine.TransformConstraintData(input.readString());
          data.order = input.readInt(true);
          for (let ii = 0, nn = input.readInt(true); ii < nn; ii++) data.bones.push(skeletonData.bones[input.readInt(true)]);
          data.target = skeletonData.bones[input.readInt(true)];
          data.local = input.readBoolean();
          data.relative = input.readBoolean();
          data.offsetRotation = input.readFloat();
          data.offsetX = input.readFloat() * scale;
          data.offsetY = input.readFloat() * scale;
          data.offsetScaleX = input.readFloat();
          data.offsetScaleY = input.readFloat();
          data.offsetShearY = input.readFloat();
          data.rotateMix = input.readFloat();
          data.translateMix = input.readFloat();
          data.scaleMix = input.readFloat();
          data.shearMix = input.readFloat();
          skeletonData.transformConstraints.push(data);
        }

        for (let i = 0, n = input.readInt(true); i < n; i++) {
          const data = new spine.PathConstraintData(input.readString());
          data.order = input.readInt(true);
          for (let ii = 0, nn = input.readInt(true); ii < nn; ii++) data.bones.push(skeletonData.bones[input.readInt(true)]);
          data.target = skeletonData.slots[input.readInt(true)];
          data.positionMode = input.readInt(true);
          data.spacingMode = input.readInt(true);
          data.rotateMode = input.readInt(true);
          data.offsetRotation = input.readFloat();
          data.position = input.readFloat();
          if (data.positionMode === spine.PositionMode.Fixed) data.position *= scale;
          data.spacing = input.readFloat();
          if (data.spacingMode === spine.SpacingMode.Length || data.spacingMode === spine.SpacingMode.Fixed) {
            data.spacing *= scale;
          }
          data.rotateMix = input.readFloat();
          data.translateMix = input.readFloat();
          skeletonData.pathConstraints.push(data);
        }

        const defaultSkin = this.readSkin(input, skeletonData, 'default', nonessential);
        if (defaultSkin) {
          skeletonData.defaultSkin = defaultSkin;
          skeletonData.skins.push(defaultSkin);
        }
        for (let i = 0, n = input.readInt(true); i < n; i++) {
          const skinName = input.readString();
          // libgdx stores null for an empty named skin; an empty Skin keeps skin lookups safe in spine-ts.
          skeletonData.skins.push(
            this.readSkin(input, skeletonData, skinName, nonessential) || new spine.Skin(skinName)
          );
        }

        for (const linkedMesh of this.linkedMeshes) {
          const skin = linkedMesh.skin == null ? skeletonData.defaultSkin : skeletonData.findSkin(linkedMesh.skin);
          if (!skin) throw new Error(`Skin not found: ${linkedMesh.skin}`);
          const parent = skin.getAttachment(linkedMesh.slotIndex, linkedMesh.parent);
          if (!parent) throw new Error(`Parent mesh not found: ${linkedMesh.parent}`);
          linkedMesh.mesh.setParentMesh(parent);
          linkedMesh.mesh.updateUVs();
        }
        this.linkedMeshes.length = 0;

        for (let i = 0, n = input.readInt(true); i < n; i++) {
          const data = new spine.EventData(input.readString());
          data.intValue = input.readInt(false);
          data.floatValue = input.readFloat();
          data.stringValue = input.readString();
          data.audioPath = input.readString();
          if (data.audioPath != null) {
            data.volume = input.readFloat();
            data.balance = input.readFloat();
          }
          skeletonData.events.push(data);
        }

        for (let i = 0, n = input.readInt(true); i < n; i++) {
          this.readAnimation(input, input.readString(), skeletonData);
        }

        if (input.position !== input.bytes.length) {
          throw new Error(
            `Spine 3.7 binary has ${input.bytes.length - input.position} unread bytes; the export may use another format version`
          );
        }
        return skeletonData;
      }

      readSkin(input, skeletonData, skinName, nonessential) {
        const slotCount = input.readInt(true);
        if (slotCount === 0) return null;
        const skin = new spine.Skin(skinName);
        for (let i = 0; i < slotCount; i++) {
          const slotIndex = input.readInt(true);
          for (let ii = 0, nn = input.readInt(true); ii < nn; ii++) {
            const name = input.readString();
            const attachment = this.readAttachment(input, skeletonData, skin, slotIndex, name, nonessential);
            if (attachment) skin.addAttachment(slotIndex, name, attachment);
          }
        }
        return skin;
      }

      readAttachment(input, skeletonData, skin, slotIndex, attachmentName, nonessential) {
        const scale = this.scale;
        let name = input.readString();
        if (name == null) name = attachmentName;

        const typeIndex = input.readByte();
        const type = ATTACHMENT_TYPES[typeIndex];
        switch (type) {
          case 'region': {
            let path = input.readString();
            const rotation = input.readFloat();
            const x = input.readFloat();
            const y = input.readFloat();
            const scaleX = input.readFloat();
            const scaleY = input.readFloat();
            const width = input.readFloat();
            const height = input.readFloat();
            const color = input.readInt32();

            if (path == null) path = name;
            const region = this.attachmentLoader.newRegionAttachment(skin, name, path);
            if (!region) return null;
            region.path = path;
            region.x = x * scale;
            region.y = y * scale;
            region.scaleX = scaleX;
            region.scaleY = scaleY;
            region.rotation = rotation;
            region.width = width * scale;
            region.height = height * scale;
            rgba8888(region.color, color);
            region.updateOffset();
            return region;
          }
          case 'boundingbox': {
            const vertexCount = input.readInt(true);
            const vertices = this.readVertices(input, vertexCount);
            const color = nonessential ? input.readInt32() : 0;

            const box = this.attachmentLoader.newBoundingBoxAttachment(skin, name);
            if (!box) return null;
            box.worldVerticesLength = vertexCount << 1;
            box.vertices = vertices.vertices;
            box.bones = vertices.bones;
            if (nonessential) rgba8888(box.color, color);
            return box;
          }
          case 'mesh': {
            let path = input.readString();
            const color = input.readInt32();
            const vertexCount = input.readInt(true);
            const uvs = this.readFloatArray(input, vertexCount << 1, 1);
            const triangles = this.readShortArray(input);
            const vertices = this.readVertices(input, vertexCount);
            const hullLength = input.readInt(true);
            let edges = null;
            let width = 0;
            let height = 0;
            if (nonessential) {
              edges = this.readShortArray(input);
              width = input.readFloat();
              height = input.readFloat();
            }

            if (path == null) path = name;
            const mesh = this.attachmentLoader.newMeshAttachment(skin, name, path);
            if (!mesh) return null;
            mesh.path = path;
            rgba8888(mesh.color, color);
            mesh.bones = vertices.bones;
            mesh.vertices = vertices.vertices;
            mesh.worldVerticesLength = vertexCount << 1;
            mesh.triangles = triangles;
            mesh.regionUVs = uvs;
            mesh.updateUVs();
            mesh.hullLength = hullLength << 1;
            if (nonessential) {
              mesh.edges = edges;
              mesh.width = width * scale;
              mesh.height = height * scale;
            }
            return mesh;
          }
          case 'linkedmesh': {
            let path = input.readString();
            const color = input.readInt32();
            const skinName = input.readString();
            const parent = input.readString();
            const inheritDeform = input.readBoolean();
            let width = 0;
            let height = 0;
            if (nonessential) {
              width = input.readFloat();
              height = input.readFloat();
            }

            if (path == null) path = name;
            const mesh = this.attachmentLoader.newMeshAttachment(skin, name, path);
            if (!mesh) return null;
            mesh.path = path;
            rgba8888(mesh.color, color);
            mesh.inheritDeform = inheritDeform;
            if (nonessential) {
              mesh.width = width * scale;
              mesh.height = height * scale;
            }
            this.linkedMeshes.push(new LinkedMesh(mesh, skinName, slotIndex, parent));
            return mesh;
          }
          case 'path': {
            const closed = input.readBoolean();
            const constantSpeed = input.readBoolean();
            const vertexCount = input.readInt(true);
            const vertices = this.readVertices(input, vertexCount);
            const lengths = spine.Utils.newArray(Math.floor(vertexCount / 3), 0);
            for (let i = 0; i < lengths.length; i++) lengths[i] = input.readFloat() * scale;
            const color = nonessential ? input.readInt32() : 0;

            const path = this.attachmentLoader.newPathAttachment(skin, name);
            if (!path) return null;
            path.closed = closed;
            path.constantSpeed = constantSpeed;
            path.worldVerticesLength = vertexCount << 1;
            path.vertices = vertices.vertices;
            path.bones = vertices.bones;
            path.lengths = lengths;
            if (nonessential) rgba8888(path.color, color);
            return path;
          }
          case 'point': {
            const rotation = input.readFloat();
            const x = input.readFloat();
            const y = input.readFloat();
            const color = nonessential ? input.readInt32() : 0;

            const point = this.attachmentLoader.newPointAttachment(skin, name);
            if (!point) return null;
            point.x = x * scale;
            point.y = y * scale;
            point.rotation = rotation;
            if (nonessential) rgba8888(point.color, color);
            return point;
          }
          case 'clipping': {
            const endSlotIndex = input.readInt(true);
            const vertexCount = input.readInt(true);
            const vertices = this.readVertices(input, vertexCount);
            const color = nonessential ? input.readInt32() : 0;

            const clip = this.attachmentLoader.newClippingAttachment(skin, name);
            if (!clip) return null;
            clip.endSlot = skeletonData.slots[endSlotIndex];
            clip.worldVerticesLength = vertexCount << 1;
            clip.vertices = vertices.vertices;
            clip.bones = vertices.bones;
            if (nonessential) rgba8888(clip.color, color);
            return clip;
          }
        }
        throw new Error(`Unknown Spine 3.7 attachment type ${typeIndex} for '${name}'`);
      }

      readVertices(input, vertexCount) {
        const verticesLength = vertexCount << 1;
        if (!input.readBoolean()) {
          return { bones: null, vertices: this.readFloatArray(input, verticesLength, this.scale) };
        }
        const weights = [];
        const bones = [];
        for (let i = 0; i < vertexCount; i++) {
          const boneCount = input.readInt(true);
          bones.push(boneCount);
          for (let ii = 0; ii < boneCount; ii++) {
            bones.push(input.readInt(true));
            weights.push(input.readFloat() * this.scale);
            weights.push(input.readFloat() * this.scale);
            weights.push(input.readFloat());
          }
        }
        return { bones, vertices: spine.Utils.toFloatArray(weights) };
      }

      readFloatArray(input, count, scale) {
        const array = spine.Utils.newFloatArray(count);
        for (let i = 0; i < count; i++) array[i] = input.readFloat() * scale;
        return array;
      }

      readShortArray(input) {
        const count = input.readInt(true);
        const array = new Array(count);
        for (let i = 0; i < count; i++) array[i] = input.readShort();
        return array;
      }

      readAnimation(input, name, skeletonData) {
        const scale = this.scale;
        const timelines = [];
        let duration = 0;
        const color = new spine.Color();
        const dark = new spine.Color();

        for (let i = 0, n = input.readInt(true); i < n; i++) {
          const slotIndex = input.readInt(true);
          for (let ii = 0, nn = input.readInt(true); ii < nn; ii++) {
            const timelineType = input.readByte();
            const frameCount = input.readInt(true);
            switch (timelineType) {
              case SLOT_ATTACHMENT: {
                const timeline = new spine.AttachmentTimeline(frameCount);
                timeline.slotIndex = slotIndex;
                for (let frame = 0; frame < frameCount; frame++) {
                  timeline.setFrame(frame, input.readFloat(), input.readString());
                }
                timelines.push(timeline);
                duration = Math.max(duration, timeline.frames[frameCount - 1]);
                break;
              }
              case SLOT_COLOR: {
                const timeline = new spine.ColorTimeline(frameCount);
                timeline.slotIndex = slotIndex;
                for (let frame = 0; frame < frameCount; frame++) {
                  const time = input.readFloat();
                  rgba8888(color, input.readInt32());
                  timeline.setFrame(frame, time, color.r, color.g, color.b, color.a);
                  if (frame < frameCount - 1) this.readCurve(input, frame, timeline);
                }
                timelines.push(timeline);
                duration = Math.max(duration, timeline.frames[(frameCount - 1) * spine.ColorTimeline.ENTRIES]);
                break;
              }
              case SLOT_TWO_COLOR: {
                const timeline = new spine.TwoColorTimeline(frameCount);
                timeline.slotIndex = slotIndex;
                for (let frame = 0; frame < frameCount; frame++) {
                  const time = input.readFloat();
                  rgba8888(color, input.readInt32());
                  rgb888(dark, input.readInt32());
                  timeline.setFrame(frame, time, color.r, color.g, color.b, color.a, dark.r, dark.g, dark.b);
                  if (frame < frameCount - 1) this.readCurve(input, frame, timeline);
                }
                timelines.push(timeline);
                duration = Math.max(duration, timeline.frames[(frameCount - 1) * spine.TwoColorTimeline.ENTRIES]);
                break;
              }
              default:
                throw new Error(`Unknown Spine 3.7 slot timeline type ${timelineType} in '${name}'`);
            }
          }
        }

        for (let i = 0, n = input.readInt(true); i < n; i++) {
          const boneIndex = input.readInt(true);
          for (let ii = 0, nn = input.readInt(true); ii < nn; ii++) {
            const timelineType = input.readByte();
            const frameCount = input.readInt(true);
            switch (timelineType) {
              case BONE_ROTATE: {
                const timeline = new spine.RotateTimeline(frameCount);
                timeline.boneIndex = boneIndex;
                for (let frame = 0; frame < frameCount; frame++) {
                  timeline.setFrame(frame, input.readFloat(), input.readFloat());
                  if (frame < frameCount - 1) this.readCurve(input, frame, timeline);
                }
                timelines.push(timeline);
                duration = Math.max(duration, timeline.frames[(frameCount - 1) * spine.RotateTimeline.ENTRIES]);
                break;
              }
              case BONE_TRANSLATE:
              case BONE_SCALE:
              case BONE_SHEAR: {
                let timeline;
                let timelineScale = 1;
                if (timelineType === BONE_SCALE) {
                  timeline = new spine.ScaleTimeline(frameCount);
                } else if (timelineType === BONE_SHEAR) {
                  timeline = new spine.ShearTimeline(frameCount);
                } else {
                  timeline = new spine.TranslateTimeline(frameCount);
                  timelineScale = scale;
                }
                timeline.boneIndex = boneIndex;
                for (let frame = 0; frame < frameCount; frame++) {
                  timeline.setFrame(
                    frame,
                    input.readFloat(),
                    input.readFloat() * timelineScale,
                    input.readFloat() * timelineScale
                  );
                  if (frame < frameCount - 1) this.readCurve(input, frame, timeline);
                }
                timelines.push(timeline);
                duration = Math.max(duration, timeline.frames[(frameCount - 1) * spine.TranslateTimeline.ENTRIES]);
                break;
              }
              default:
                throw new Error(`Unknown Spine 3.7 bone timeline type ${timelineType} in '${name}'`);
            }
          }
        }

        for (let i = 0, n = input.readInt(true); i < n; i++) {
          const index = input.readInt(true);
          const frameCount = input.readInt(true);
          const timeline = new spine.IkConstraintTimeline(frameCount);
          timeline.ikConstraintIndex = index;
          for (let frame = 0; frame < frameCount; frame++) {
            timeline.setFrame(
              frame,
              input.readFloat(),
              input.readFloat(),
              input.readByte(),
              input.readBoolean(),
              input.readBoolean()
            );
            if (frame < frameCount - 1) this.readCurve(input, frame, timeline);
          }
          timelines.push(timeline);
          duration = Math.max(duration, timeline.frames[(frameCount - 1) * spine.IkConstraintTimeline.ENTRIES]);
        }

        for (let i = 0, n = input.readInt(true); i < n; i++) {
          const index = input.readInt(true);
          const frameCount = input.readInt(true);
          const timeline = new spine.TransformConstraintTimeline(frameCount);
          timeline.transformConstraintIndex = index;
          for (let frame = 0; frame < frameCount; frame++) {
            timeline.setFrame(
              frame,
              input.readFloat(),
              input.readFloat(),
              input.readFloat(),
              input.readFloat(),
              input.readFloat()
            );
            if (frame < frameCount - 1) this.readCurve(input, frame, timeline);
          }
          timelines.push(timeline);
          duration = Math.max(duration, timeline.frames[(frameCount - 1) * spine.TransformConstraintTimeline.ENTRIES]);
        }

        for (let i = 0, n = input.readInt(true); i < n; i++) {
          const index = input.readInt(true);
          const data = skeletonData.pathConstraints[index];
          for (let ii = 0, nn = input.readInt(true); ii < nn; ii++) {
            const timelineType = input.readByte();
            const frameCount = input.readInt(true);
            switch (timelineType) {
              case PATH_POSITION:
              case PATH_SPACING: {
                let timeline;
                let timelineScale = 1;
                if (timelineType === PATH_SPACING) {
                  timeline = new spine.PathConstraintSpacingTimeline(frameCount);
                  if (data.spacingMode === spine.SpacingMode.Length || data.spacingMode === spine.SpacingMode.Fixed) {
                    timelineScale = scale;
                  }
                } else {
                  timeline = new spine.PathConstraintPositionTimeline(frameCount);
                  if (data.positionMode === spine.PositionMode.Fixed) timelineScale = scale;
                }
                timeline.pathConstraintIndex = index;
                for (let frame = 0; frame < frameCount; frame++) {
                  timeline.setFrame(frame, input.readFloat(), input.readFloat() * timelineScale);
                  if (frame < frameCount - 1) this.readCurve(input, frame, timeline);
                }
                timelines.push(timeline);
                duration = Math.max(
                  duration,
                  timeline.frames[(frameCount - 1) * spine.PathConstraintPositionTimeline.ENTRIES]
                );
                break;
              }
              case PATH_MIX: {
                const timeline = new spine.PathConstraintMixTimeline(frameCount);
                timeline.pathConstraintIndex = index;
                for (let frame = 0; frame < frameCount; frame++) {
                  timeline.setFrame(frame, input.readFloat(), input.readFloat(), input.readFloat());
                  if (frame < frameCount - 1) this.readCurve(input, frame, timeline);
                }
                timelines.push(timeline);
                duration = Math.max(duration, timeline.frames[(frameCount - 1) * spine.PathConstraintMixTimeline.ENTRIES]);
                break;
              }
              default:
                throw new Error(`Unknown Spine 3.7 path timeline type ${timelineType} in '${name}'`);
            }
          }
        }

        for (let i = 0, n = input.readInt(true); i < n; i++) {
          const skin = skeletonData.skins[input.readInt(true)];
          for (let ii = 0, nn = input.readInt(true); ii < nn; ii++) {
            const slotIndex = input.readInt(true);
            for (let iii = 0, nnn = input.readInt(true); iii < nnn; iii++) {
              const attachmentName = input.readString();
              const attachment = skin.getAttachment(slotIndex, attachmentName);
              if (!attachment) throw new Error(`Deform attachment not found: ${attachmentName}`);
              const weighted = attachment.bones != null;
              const vertices = attachment.vertices;
              const deformLength = weighted ? (vertices.length / 3) * 2 : vertices.length;

              const frameCount = input.readInt(true);
              const timeline = new spine.DeformTimeline(frameCount);
              timeline.slotIndex = slotIndex;
              timeline.attachment = attachment;

              for (let frame = 0; frame < frameCount; frame++) {
                const time = input.readFloat();
                let deform;
                let end = input.readInt(true);
                if (end === 0) {
                  deform = weighted ? spine.Utils.newFloatArray(deformLength) : vertices;
                } else {
                  deform = spine.Utils.newFloatArray(deformLength);
                  const start = input.readInt(true);
                  end += start;
                  for (let v = start; v < end; v++) deform[v] = input.readFloat() * scale;
                  if (!weighted) {
                    for (let v = 0; v < deform.length; v++) deform[v] += vertices[v];
                  }
                }
                timeline.setFrame(frame, time, deform);
                if (frame < frameCount - 1) this.readCurve(input, frame, timeline);
              }
              timelines.push(timeline);
              duration = Math.max(duration, timeline.frames[frameCount - 1]);
            }
          }
        }

        const drawOrderCount = input.readInt(true);
        if (drawOrderCount > 0) {
          const timeline = new spine.DrawOrderTimeline(drawOrderCount);
          const slotCount = skeletonData.slots.length;
          for (let i = 0; i < drawOrderCount; i++) {
            const time = input.readFloat();
            const offsetCount = input.readInt(true);
            const drawOrder = spine.Utils.newArray(slotCount, -1);
            const unchanged = spine.Utils.newArray(slotCount - offsetCount, 0);
            let originalIndex = 0;
            let unchangedIndex = 0;
            for (let ii = 0; ii < offsetCount; ii++) {
              const slotIndex = input.readInt(true);
              while (originalIndex !== slotIndex) unchanged[unchangedIndex++] = originalIndex++;
              drawOrder[originalIndex + input.readInt(true)] = originalIndex++;
            }
            while (originalIndex < slotCount) unchanged[unchangedIndex++] = originalIndex++;
            for (let ii = slotCount - 1; ii >= 0; ii--) {
              if (drawOrder[ii] === -1) drawOrder[ii] = unchanged[--unchangedIndex];
            }
            timeline.setFrame(i, time, drawOrder);
          }
          timelines.push(timeline);
          duration = Math.max(duration, timeline.frames[drawOrderCount - 1]);
        }

        const eventCount = input.readInt(true);
        if (eventCount > 0) {
          const timeline = new spine.EventTimeline(eventCount);
          for (let i = 0; i < eventCount; i++) {
            const time = input.readFloat();
            const eventData = skeletonData.events[input.readInt(true)];
            const event = new spine.Event(time, eventData);
            event.intValue = input.readInt(false);
            event.floatValue = input.readFloat();
            event.stringValue = input.readBoolean() ? input.readString() : eventData.stringValue;
            if (eventData.audioPath != null) {
              event.volume = input.readFloat();
              event.balance = input.readFloat();
            }
            timeline.setFrame(i, event);
          }
          timelines.push(timeline);
          duration = Math.max(duration, timeline.frames[eventCount - 1]);
        }

        skeletonData.animations.push(new spine.Animation(name, timelines, duration));
      }

      readCurve(input, frameIndex, timeline) {
        switch (input.readByte()) {
          case CURVE_STEPPED:
            timeline.setStepped(frameIndex);
            break;
          case CURVE_BEZIER:
            timeline.setCurve(frameIndex, input.readFloat(), input.readFloat(), input.readFloat(), input.readFloat());
            break;
        }
      }
    }

    spine.SkeletonBinary = SkeletonBinary;
    return SkeletonBinary;
  }

  return { install, BinaryInput, readHeader };
});
