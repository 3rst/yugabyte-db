package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.annotation.JsonBackReference;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.ImageBundleUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.ManyToOne;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Entity
@Data
@EqualsAndHashCode(callSuper = false)
public class ImageBundle extends Model {

  @ApiModelProperty(value = "Image Bundle UUID", accessMode = READ_ONLY)
  @Id
  private UUID uuid;

  @ApiModelProperty(value = "Image Bundle Name")
  private String name;

  @ManyToOne
  @JsonBackReference("provider-image-bundles")
  private Provider provider;

  @ApiModelProperty(value = "Image Bundle Details")
  @DbJson
  private ImageBundleDetails details = new ImageBundleDetails();

  @ApiModelProperty(
      value = "Default Image Bundle. A provider can have two defaults, one per architecture")
  @Column(name = "is_default")
  private Boolean useAsDefault = false;

  @Data
  @EqualsAndHashCode(callSuper = false)
  public static class NodeProperties {
    private String machineImage;
    private String sshUser;
    private Integer sshPort;
  }

  public static final Finder<UUID, ImageBundle> find =
      new Finder<UUID, ImageBundle>(ImageBundle.class) {};

  public static ImageBundle getOrBadRequest(UUID providerUUID, UUID imageBundleUUID) {
    ImageBundle bundle = ImageBundle.get(providerUUID, imageBundleUUID);
    if (bundle == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Invalid ImageBundle UUID: " + imageBundleUUID);
    }
    return bundle;
  }

  public static ImageBundle getOrBadRequest(UUID imageBundleUUID) {
    ImageBundle bundle = ImageBundle.get(imageBundleUUID);
    if (bundle == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Invalid ImageBundle UUID: " + imageBundleUUID);
    }
    return bundle;
  }

  public static ImageBundle get(UUID imageBundleUUID) {
    return find.byId(imageBundleUUID);
  }

  public static ImageBundle get(UUID providerUUID, UUID imageBundleUUID) {
    return find.query().where().eq("provider_uuid", providerUUID).idEq(imageBundleUUID).findOne();
  }

  public static List<ImageBundle> getAll(UUID providerUUID) {
    return find.query().where().eq("provider_uuid", providerUUID).findList();
  }

  public static ImageBundle create(
      Provider provider, String name, ImageBundleDetails details, boolean isDefault) {
    ImageBundle bundle = new ImageBundle();
    bundle.setProvider(provider);
    bundle.setName(name);
    bundle.setDetails(details);
    bundle.setUseAsDefault(isDefault);
    bundle.save();
    return bundle;
  }

  public static List<ImageBundle> getDefaultForProvider(UUID providerUUID) {
    // At a given time, two defaults can exist in image bundle one for `x86` & other for `arm`.
    return find.query().where().eq("provider_uuid", providerUUID).eq("is_default", true).findList();
  }

  public static List<ImageBundle> getBundlesForArchType(UUID providerUUID, String arch) {
    if (arch.isEmpty()) {
      // List all the bundles for the provider (non-AWS provider case).
      return find.query().where().eq("provider_uuid", providerUUID).findList();
    }
    // At a given time, two defaults can exist in image bundle one for `x86` & other for `arm`.
    List<ImageBundle> bundles;
    try {
      bundles =
          find.query()
              .where()
              .eq("provider_uuid", providerUUID)
              .eq("details::json->>'arch'", arch)
              .findList();
    } catch (Exception e) {
      // In case exception is thrown we will fallback to manual filtering, specifically for UTs
      bundles = find.query().where().eq("provider_uuid", providerUUID).findList();
      bundles.removeIf(
          bundle -> {
            if (bundle.getDetails() != null
                && bundle.getDetails().getArch().toString().equals(arch)) {
              return false;
            }
            return true;
          });
    }

    return bundles;
  }

  @JsonIgnore
  public boolean isUpdateNeeded(ImageBundle bundle) {
    return !Objects.equals(this.getUseAsDefault(), bundle.getUseAsDefault())
        || !Objects.equals(this.getDetails(), bundle.getDetails());
  }

  @JsonIgnore
  public long getUniverseCount() {
    Provider provider = this.provider;
    if (provider == null) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Image Bundle needs to be associated to a provider!");
    }
    List<ImageBundle> defaultImageBundle = ImageBundle.getDefaultForProvider(provider.getUuid());
    Set<Universe> universes =
        Customer.get(provider.getCustomerUUID()).getUniversesForProvider(provider.getUuid());
    Set<Universe> universeUsingImageBundle =
        universes.stream()
            .filter(u -> checkImageBudleInCluster(u, uuid, defaultImageBundle))
            .collect(Collectors.toSet());

    return universeUsingImageBundle.stream().count();
  }

  @JsonIgnore
  public static int getImageBundleCount(UUID providerUUID) {
    return find.query().where().eq("provider_uuid", providerUUID).findCount();
  }

  @JsonIgnore
  private boolean checkImageBudleInCluster(
      Universe universe, UUID imageBundleUUID, List<ImageBundle> defaultBundles) {
    Architecture arch = universe.getUniverseDetails().arch;
    ImageBundle defaultBundle = ImageBundleUtil.getDefaultBundleForUniverse(arch, defaultBundles);
    for (Cluster cluster : universe.getUniverseDetails().clusters) {
      if (cluster.userIntent.imageBundleUUID == null
          && imageBundleUUID.equals(defaultBundle.getUuid())) {
        return true;
      } else if (cluster.userIntent.imageBundleUUID != null
          && cluster.userIntent.imageBundleUUID.equals(imageBundleUUID)) {
        return true;
      }
    }
    return false;
  }
}
