#include "pch.h"
#include "msvrContext.h"


msvrContext::msvrContext()
{
}

msvrContext::~msvrContext()
{
}

msvrSettings& msvrContext::getSettings()
{
    return m_settings;
}

void msvrContext::send(bool force)
{
    if (m_sender.isSending()) {
        // previous request is not completed yet
        return;
    }

    if (force) {
        m_material_manager.makeDirtyAll();
        m_entity_manager.makeDirtyAll();
    }

    for (auto& pair : m_buffer_records) {
        auto& buf = pair.second;
        if (buf.dst_mesh)
            m_entity_manager.add(buf.dst_mesh);
    }

    // build material list
    {
        char name[128];
        for (auto& mr : m_material_records) {
            auto mat = ms::Material::create();
            sprintf(name, "VREDMaterial:ID[%04x]", mr.id);
            mat->id = mr.id;
            mat->index = mr.id;
            mat->name = name;

            auto& stdmat = ms::AsStandardSpecMaterial(*mat);
            if (mr.diffuse_color != float4::zero())
                stdmat.setColor(mr.diffuse_color);
            if (mr.bump_scale != 0.0f)
                stdmat.setBumpScale(mr.bump_scale);
            if (mr.specular_color != float4::zero())
                stdmat.setSpecularColor(mr.specular_color);

            auto get_texid = [this, &mr](int slot) -> int {
                if (slot == ms::InvalidID)
                    return ms::InvalidID;
                auto& rec = m_texture_records[mr.texture_slots[slot]];
                return rec.dst ? rec.dst->id : ms::InvalidID;
            };
            {
                int tid = get_texid(mr.color_map);
                if (tid != ms::InvalidID)
                    stdmat.setColorMap(tid);
            }
            {
                int tid = get_texid(mr.bump_map);
                if (tid != ms::InvalidID)
                    stdmat.setBumpMap(tid);
            }
            {
                int tid = get_texid(mr.specular_map);
                if (tid != ms::InvalidID)
                    stdmat.setSpecularGlossMap(tid);
            }
            m_material_manager.add(mat);
        }
        m_material_records.clear();
    }

    // camera
    if (m_settings.sync_camera) {
        if (!m_camera) {
            m_camera = ms::Camera::create();
        }
        m_camera->path = m_settings.camera_path;
        m_camera->position = m_camera_pos;
        m_camera->rotation = m_camera_rot;
        m_camera->fov = m_camera_fov;
        m_camera->near_plane = m_camera_near;
        m_camera->far_plane = m_camera_far;
        m_camera_dirty = false;
        m_entity_manager.add(m_camera);
    }

    for (auto& kvp : m_texture_records) {
        auto& rec = kvp.second;
        if (rec.dst && rec.dirty && rec.used) {
            m_texture_manager.add(rec.dst);
            rec.dirty = false;
        }
    }


    m_sender.on_prepare = [this]() {
        // handle deleted objects
        for (auto h : m_meshes_deleted) {
            char path[128];
            sprintf(path, "/VREDMesh:ID[%08x]", h);
            m_entity_manager.erase(ms::Identifier(path, (int)h));
        }
        m_meshes_deleted.clear();


        auto& t = m_sender;
        t.client_settings = m_settings.client_settings;
        t.scene_settings.handedness = ms::Handedness::LeftZUp;
        t.scene_settings.scale_factor = m_settings.scale_factor;

        t.textures = m_texture_manager.getDirtyTextures();
        t.materials = m_material_manager.getDirtyMaterials();
        t.transforms = m_entity_manager.getDirtyTransforms();
        t.geometries = m_entity_manager.getDirtyGeometries();
        t.deleted_entities = m_entity_manager.getDeleted();
    };
    m_sender.on_succeeded = [this]() {
        m_material_ids.clearDirtyFlags();
        m_texture_manager.clearDirtyFlags();
        m_material_manager.clearDirtyFlags();
        m_entity_manager.clearDirtyFlags();
    };
    m_sender.kick();
}


void msvrContext::onGenTextures(GLsizei n, GLuint * textures)
{
    for (int i = 0; i < (int)n; ++i) {
        m_texture_records[textures[i]];
    }
}

void msvrContext::onDeleteTextures(GLsizei n, const GLuint * textures)
{
    for (int i = 0; i < (int)n; ++i) {
        m_texture_records.erase(textures[i]);
    }
}

void msvrContext::onActiveTexture(GLenum texture)
{
    m_active_texture = texture - GL_TEXTURE0;
}

void msvrContext::onBindTexture(GLenum target, GLuint texture)
{
    m_texture_slots[m_active_texture] = texture;
}

void msvrContext::onTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid * data)
{
    if (data && level == 0) {
        auto handle = m_texture_slots[m_active_texture];
        auto& rec = m_texture_records[handle];

        int pixel_size = 0;
        ms::TextureFormat msf = ms::TextureFormat::Unknown;
        switch (format) {
        case GL_RED:
            switch (type) {
            case GL_UNSIGNED_BYTE:
                pixel_size = 1;
                msf = ms::TextureFormat::Ru8;
                break;
            case GL_HALF_FLOAT:
                pixel_size = 2;
                msf = ms::TextureFormat::Rf16;
                break;
            case GL_FLOAT:
                pixel_size = 4;
                msf = ms::TextureFormat::Rf32;
                break;
            }
            break;
        case GL_RG:
            switch (type) {
            case GL_UNSIGNED_BYTE:
                pixel_size = 2;
                msf = ms::TextureFormat::RGu8;
                break;
            case GL_HALF_FLOAT:
                pixel_size = 4;
                msf = ms::TextureFormat::RGf16;
                break;
            case GL_FLOAT:
                pixel_size = 8;
                msf = ms::TextureFormat::RGf32;
                break;
            }
            break;
        case GL_RGB:
            switch (type) {
            case GL_UNSIGNED_BYTE:
                pixel_size = 3;
                msf = ms::TextureFormat::RGBu8;
                break;
            case GL_HALF_FLOAT:
                pixel_size = 6;
                msf = ms::TextureFormat::RGBf16;
                break;
            case GL_FLOAT:
                pixel_size = 12;
                msf = ms::TextureFormat::RGBf32;
                break;
            }
            break;
        case GL_RGBA:
            switch (type) {
            case GL_UNSIGNED_BYTE:
                pixel_size = 4;
                msf = ms::TextureFormat::RGBAu8;
                break;
            case GL_HALF_FLOAT:
                pixel_size = 8;
                msf = ms::TextureFormat::RGBAf16;
                break;
            case GL_FLOAT:
                pixel_size = 16;
                msf = ms::TextureFormat::RGBAf32;
                break;
            }
            break;
        }

        if (pixel_size) {
            auto dst = ms::Texture::create();
            char name[128];
            sprintf(name, "VREDTexture_ID%08x", handle);
            dst->name = name;
            dst->id = (int)handle;
            dst->format = msf;
            dst->width = width;
            dst->height = height;

            int size = width * height * pixel_size;
            dst->data.assign((char*)data, (char*)data + size);
            rec.dst = dst;
            rec.dirty = true;
        }
        else {
            msLogWarning("unsupported texture format\n");
        }
    }
}

void msvrContext::onGenFramebuffers(GLsizei n, GLuint * ids)
{
}

void msvrContext::onBindFramebuffer(GLenum target, GLuint framebuffer)
{
    if (target != GL_FRAMEBUFFER)
        return;
    m_fb_handle = framebuffer;
}

void msvrContext::onDeleteFramebuffers(GLsizei n, GLuint *framebuffers)
{
    for (int i = 0; i < n; ++i)
        m_framebuffer_records.erase(framebuffers[i]);
}

void msvrContext::onFramebufferTexture(GLenum target, GLenum attachment, GLuint texture, GLint level)
{
    if (target != GL_FRAMEBUFFER)
        return;

    auto& rec = m_framebuffer_records[m_fb_handle];
    if (attachment == GL_DEPTH_ATTACHMENT)
        rec.depth_stencil = texture;
    else
        rec.colors[attachment - GL_COLOR_ATTACHMENT0] = texture;
}

void msvrContext::onFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
    if (target != GL_FRAMEBUFFER)
        return;

    auto& rec = m_framebuffer_records[m_fb_handle];
    if (attachment == GL_DEPTH_ATTACHMENT)
        rec.depth_stencil = texture;
    else
        rec.colors[attachment - GL_COLOR_ATTACHMENT0] = texture;
}


void msvrContext::onGenBuffers(GLsizei n, GLuint *buffers)
{
    for (int i = 0; i < (int)n; ++i) {
        auto it = std::find(m_meshes_deleted.begin(), m_meshes_deleted.end(), buffers[i]);
        if (it != m_meshes_deleted.end()) {
            m_meshes_deleted.erase(it);
        }
    }
}

void msvrContext::onDeleteBuffers(GLsizei n, const GLuint *buffers)
{
    for (int i = 0; i < n; ++i) {
        auto it = m_buffer_records.find(buffers[i]);
        if (it != m_buffer_records.end()) {
            auto& rec = it->second;
            if (rec.dst_mesh)
                m_entity_manager.erase(rec.dst_mesh->getIdentifier());
            m_buffer_records.erase(it);
        }
    }
}

void msvrContext::onBindBuffer(GLenum target, GLuint buffer)
{
    switch (target) {
    case GL_ARRAY_BUFFER:
        m_vb_handle = buffer;
        break;
    case GL_ELEMENT_ARRAY_BUFFER:
        m_ib_handle = buffer;
        break;
    case GL_UNIFORM_BUFFER:
        m_ub_handle = buffer;
        break;
    }
}

void msvrContext::onBindVertexBuffer(GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride)
{
    m_vb_handle = buffer;
    auto& rec = m_buffer_records[buffer];
    rec.stride = stride;
}

void msvrContext::onBindBufferBase(GLenum target, GLuint index, GLuint buffer)
{
    switch (target) {
    case GL_UNIFORM_BUFFER:
        m_ub_handles[index] = buffer;
        break;
    }
}

void msvrContext::onBufferData(GLenum target, GLsizeiptr size, const void * data, GLenum usage)
{
    if (auto *buf = getActiveBuffer(target)) {
        buf->data.resize_discard(size);
        if (data) {
            memcpy(buf->data.data(), data, buf->data.size());
            buf->dirty = true;
        }
    }
}

void msvrContext::onNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizei size, const void * data)
{
    auto *buf = &m_buffer_records[buffer];
    buf->data.resize_discard(offset + size);
    if (data) {
        memcpy(buf->data.data() + offset, data, size);
        buf->dirty = true;
    }
}

void msvrContext::onMapBuffer(GLenum target, GLenum access, void *& mapped_data)
{
    if (access != GL_WRITE_ONLY) {
        return;
    }

    // mapped memory returned by glMapBuffer() is a special kind of memory and reading it is exetemery slow.
    // so make temporary memory and return it to application, and copy it to actual mapped memory later (onUnmapBuffer()).
    if (auto *buf = getActiveBuffer(target)) {
        buf->mapped_data = mapped_data;
        buf->tmp_data.resize_discard(buf->data.size());
        mapped_data = buf->tmp_data.data();
    }
}

void msvrContext::onMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access, void *& mapped_data)
{
    if ((access & GL_MAP_WRITE_BIT) == 0) {
        return;
    }

    // same as onMapBuffer()
    if (auto *buf = getActiveBuffer(target)) {
        buf->mapped_data = mapped_data;
        buf->tmp_data.resize_discard(buf->data.size());
        mapped_data = buf->tmp_data.data() + offset;
    }
}

void msvrContext::onUnmapBuffer(GLenum target)
{
    if (auto *buf = getActiveBuffer(target)) {
        if (buf->mapped_data) {
            memcpy(buf->mapped_data, buf->tmp_data.data(), buf->tmp_data.size());
            if (buf->data != buf->tmp_data) {
                std::swap(buf->data, buf->tmp_data);
                buf->dirty = true;
            }
            buf->mapped_data = nullptr;
        }
    }
}

void msvrContext::onFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length)
{
    if (auto *buf = getActiveBuffer(target)) {
        if (buf->mapped_data) {
            memcpy((char*)buf->mapped_data, buf->tmp_data.data() + offset, length);
            if (memcmp(buf->data.data() + offset, buf->tmp_data.data() + offset, length) != 0) {
                memcpy(buf->data.data() + offset, buf->data.data() + offset, length);
                buf->dirty = true;
            }
            buf->mapped_data = nullptr;
        }
    }
}


void msvrContext::onGenVertexArrays(GLsizei n, GLuint *buffers)
{
}

void msvrContext::onDeleteVertexArrays(GLsizei n, const GLuint *buffers)
{
}

void msvrContext::onBindVertexArray(GLuint buffer)
{
}

void msvrContext::onEnableVertexAttribArray(GLuint index)
{
}

void msvrContext::onDisableVertexAttribArray(GLuint index)
{
}

void msvrContext::onVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void * pointer)
{
    if (auto *buf = getActiveBuffer(GL_ARRAY_BUFFER)) {
        buf->stride = stride;
    }
}

extern void(*WINAPI _glGetProgramiv)(GLuint program, GLenum pname, GLint *params);
extern void(*WINAPI _glGetActiveUniform)(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name);
extern GLint(*WINAPI _glGetUniformLocation)(GLuint program, const GLchar *name);

void msvrContext::onLinkProgram(GLuint program)
{
}

void msvrContext::onDeleteProgram(GLuint program)
{
    m_program_records.erase(program);
}

void msvrContext::onUseProgram(GLuint program)
{
    m_program_handle = program;

    auto& rec = m_program_records[program];
    if (rec.uniforms.empty()) {
        rec.mrec.program = program;
        int num_uniforms = 0;
        _glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &num_uniforms);
        for (int ui = 0; ui < num_uniforms; ++ui) {
            char name[128];
            GLsizei len = 0, size = 0;
            GLenum type;
            _glGetActiveUniform(program, ui, sizeof(name), &len, &size, &type, name);

            // ignore uniform block
            if (strstr(name, "."))
                continue;

            auto mstype = ms::MaterialProperty::Type::Unknown;
            switch (type) {
            case GL_FLOAT:
                mstype = ms::MaterialProperty::Type::Float;
                break;
            case GL_FLOAT_VEC2:
            case GL_FLOAT_VEC3:
            case GL_FLOAT_VEC4:
                mstype = ms::MaterialProperty::Type::Vector;
                break;
            case GL_FLOAT_MAT2:
            case GL_FLOAT_MAT3:
            case GL_FLOAT_MAT4:
                mstype = ms::MaterialProperty::Type::Matrix;
                break;
            case GL_SAMPLER_2D:
            case GL_SAMPLER_2D_ARRAY:
            case GL_SAMPLER_2D_MULTISAMPLE:
            case GL_SAMPLER_2D_MULTISAMPLE_ARRAY:
            case GL_SAMPLER_2D_RECT:
            case GL_INT_SAMPLER_2D:
            case GL_INT_SAMPLER_2D_ARRAY:
            case GL_INT_SAMPLER_2D_MULTISAMPLE_ARRAY:
            case GL_INT_SAMPLER_2D_RECT:
            case GL_UNSIGNED_INT_SAMPLER_2D:
            case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
            case GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE_ARRAY:
            case GL_UNSIGNED_INT_SAMPLER_2D_RECT:
                mstype = ms::MaterialProperty::Type::Texture;
                break;
            }

            GLuint index = _glGetUniformLocation(program, name);
            auto& uni = rec.uniforms[index];
            uni.name = name;
            uni.type = mstype;
            uni.size = size;
        }
    }
}

void msvrContext::onUniform1i(GLint location, GLint v0)
{
    if (auto *prop = findUniform(location)) {
        if (prop->type == ms::MaterialProperty::Type::Texture) {
            auto& mr = m_program_records[m_program_handle].mrec;
            if (prop->name == "colorTexture" || prop->name == "colorMap") {
                mr.color_map = v0;
            }
            else if (prop->name == "bumpTexture") {
                mr.bump_map = v0;
            }
            else if (prop->name == "specularTexture") {
                mr.specular_map = v0;
            }

        }
    }
}

void msvrContext::onUniform1fv(GLint location, GLsizei count, const GLfloat * value)
{
    if (auto *prop = findUniform(location)) {
        auto& mr = m_program_records[m_program_handle].mrec;
        if (prop->name == "bumpIntensity")
            mr.bump_scale= *value;
    }
}

void msvrContext::onUniform2fv(GLint location, GLsizei count, const GLfloat * value)
{
}

void msvrContext::onUniform3fv(GLint location, GLsizei count, const GLfloat * value)
{
    if (auto *prop = findUniform(location)) {
        auto& mr = m_program_records[m_program_handle].mrec;
        if (prop->name == "diffuseColor" || prop->name == "exteriorColor")
            (float3&)mr.diffuse_color = *(float3*)value;
        else if (prop->name == "specularColor")
            (float3&)mr.specular_color = *(float3*)value;
    }
}

void msvrContext::onUniform4fv(GLint location, GLsizei count, const GLfloat * value)
{
    if (auto *prop = findUniform(location)) {
        if (prop->name == "") {

        }
    }
}

void msvrContext::onUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat * value)
{
}

void msvrContext::onUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat * value)
{
}

void msvrContext::onUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat * value)
{
}


void msvrContext::onDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid * indices)
{
    if (mode != GL_TRIANGLES)
        return;

    auto& fb = m_framebuffer_records[m_fb_handle];
    if (!fb.isMainTarget())
        return;

    auto *vb = getActiveBuffer(GL_ARRAY_BUFFER);
    auto *ib = getActiveBuffer(GL_ELEMENT_ARRAY_BUFFER);
    if (!ib || !vb || vb->stride != sizeof(vr_vertex))
        return;

    auto& camera_buf = m_buffer_records[m_ub_handles[1]];
    auto& obj_buf = m_buffer_records[m_ub_handles[2]];
    if (camera_buf.data.size() != 992 || obj_buf.data.size() != 560)
        return;

    // camera
    {
        auto view = (float4x4&)camera_buf.data[64 * 4];
        auto proj = (float4x4&)camera_buf.data[0];

        // projection matrix -> fov, aspect, clippling planes
        float fov, aspect, near_plane, far_plane;
        extract_projection_data(proj, fov, aspect, far_plane, near_plane);

        // fov >= 180.0f means came is not perspective. capture only when camera is perspective.
        if (fov < 179.0f) {
            if (fov != m_camera_fov) {
                m_camera_dirty = true;
                m_camera_fov = fov;
                m_camera_near = near_plane;
                m_camera_far = far_plane;
            }

            // modelview matrix -> camera pos & rot
            float3 pos;
            quatf rot;
            extract_look_data(view, pos, rot);
            rot *= mu::rotateX(-90.0f * mu::Deg2Rad);

            if (pos != m_camera_pos || rot != m_camera_rot)
            {
                m_camera_dirty = true;
                m_camera_pos = pos;
                m_camera_rot = rot;
            }
        }
    }

    // textures
    for (auto& h : m_texture_slots) {
        auto& rec = m_texture_records[h];
        if (rec.dst)
            rec.used = true;
    }

    // material
    {
        auto& prec = m_program_records[m_program_handle];
        auto mrec = prec.mrec;
        std::copy(m_texture_slots, m_texture_slots + msvrMaxTextureSlots, mrec.texture_slots);

        auto it = std::find(m_material_records.begin(), m_material_records.end(), mrec);
        if (it != m_material_records.end()) {
            vb->material_id = it->id;
        }
        else {
            mrec.id = m_material_ids.getID(mrec);
            vb->material_id = mrec.id;
            m_material_records.push_back(mrec);
        }
    }

    auto transform = (float4x4&)obj_buf.data[0];
    auto task = [this, vb, ib, count, type, transform]() {
        if (!vb->dst_mesh) {
            vb->dst_mesh = ms::Mesh::create();

            char path[128];
            sprintf(path, "/VREDMesh:ID[%08x]", m_vb_handle);
            vb->dst_mesh->path = path;
        }
        auto& dst = *vb->dst_mesh;

        dst.position = extract_position(transform);
        dst.rotation = extract_rotation(transform);
        dst.scale = extract_scale(transform);

        if (vb->dirty) {
            size_t num_indices = count;
            size_t num_triangles = count / 3;
            size_t num_vertices = vb->data.size() / vb->stride;

            // convert vertices
            dst.points.resize_discard(num_vertices);
            dst.normals.resize_discard(num_vertices);
            dst.uv0.resize_discard(num_vertices);
            auto *vtx = (vr_vertex*)vb->data.data();
            for (size_t vi = 0; vi < num_vertices; ++vi) {
                dst.points[vi] = vtx[vi].vertex;
                dst.normals[vi] = vtx[vi].normal;
                dst.uv0[vi] = float2{ 1.0f, 1.0f } -vtx[vi].uv;
            }

            // convert indices
            dst.counts.resize(num_triangles, 3);
            dst.material_ids.resize(num_triangles, vb->material_id);
            if (type == GL_UNSIGNED_INT) {
                int *src = (int*)ib->data.data();
                dst.indices.assign(src, src + num_indices);
            }
            else if (type == GL_UNSIGNED_SHORT) {
                uint16_t *src = (uint16_t*)ib->data.data();
                dst.indices.resize_discard(num_indices);
                for (size_t ii = 0; ii < num_indices; ++ii)
                    dst.indices[ii] = src[ii];
            }

            dst.setupFlags();
            dst.flags.has_refine_settings = 1;
            dst.refine_settings.flags.swap_faces = true;
            dst.refine_settings.flags.gen_tangents = 1;
        }
    };
    task();
    vb->dirty = false;
}

void msvrContext::onFlush()
{
    if (m_settings.auto_sync)
        send(false);
}

BufferRecord* msvrContext::getActiveBuffer(GLenum target)
{
    switch (target) {
    case GL_ARRAY_BUFFER:
        return &m_buffer_records[m_vb_handle];
    case GL_ELEMENT_ARRAY_BUFFER:
        return &m_buffer_records[m_ib_handle];
    case GL_UNIFORM_BUFFER:
        return &m_buffer_records[m_ub_handle];
    }
    return nullptr;
}

ProgramRecord::Uniform* msvrContext::findUniform(GLint location)
{
    auto& uniforms = m_program_records[m_program_handle].uniforms;
    auto it = uniforms.find(location);
    return it != uniforms.end() ? &it->second : nullptr;
}


msvrContext* msvrGetContext()
{
    static std::unique_ptr<msvrContext> s_ctx;
    if (!s_ctx) {
        s_ctx.reset(new msvrContext());
    }
    return s_ctx.get();
}

bool FramebufferRecord::isMainTarget() const
{
    return colors[0] && colors[1] && depth_stencil;
}
